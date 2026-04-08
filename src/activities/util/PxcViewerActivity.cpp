#include "PxcViewerActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include "Epub/converters/DirectPixelWriter.h"
#include "components/UITheme.h"
#include "fontIds.h"

PxcViewerActivity::PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("PxcViewer", renderer, mappedInput), filePath(std::move(path)) {}

void PxcViewerActivity::onEnter() {
  Activity::onEnter();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);

  FsFile file;
  if (!Storage.openFileForRead("PXC", filePath, file)) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2, "Could not open file");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  uint16_t pxcWidth, pxcHeight;
  if (file.read(&pxcWidth, 2) != 2 || file.read(&pxcHeight, 2) != 2) {
    LOG_ERR("PXC", "Header read failed: %s", filePath.c_str());
    file.close();
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2, "Invalid PXC file");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  if (abs(pxcWidth - screenWidth) > 1 || abs(pxcHeight - screenHeight) > 1) {
    LOG_ERR("PXC", "PXC size %dx%d does not match screen %dx%d", pxcWidth, pxcHeight, screenWidth, screenHeight);
    file.close();
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2, "PXC size mismatch");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  const uint32_t dataOffset = file.position();
  GUI.fillPopupProgress(renderer, popupRect, 50);

  struct PxcCtx {
    FsFile* file;
    uint32_t dataOffset;
    int width, height;
    MappedInputManager::Labels labels;
  };
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  PxcCtx ctx{&file, dataOffset, pxcWidth, pxcHeight, labels};

  renderer.renderGrayscale(
      GfxRenderer::GrayscaleMode::FactoryQuality,
      [](const GfxRenderer& r, const void* raw) {
        const auto* c = static_cast<const PxcCtx*>(raw);
        c->file->seek(c->dataOffset);

        const int bytesPerRow = (c->width + 3) / 4;
        uint8_t* rowBuf = static_cast<uint8_t*>(malloc(bytesPerRow));
        if (!rowBuf) {
          LOG_ERR("PXC", "malloc failed for rowBuf (%d bytes, %dx%d)", bytesPerRow, c->width, c->height);
          return;
        }

        DirectPixelWriter pw;
        pw.init(r);

        for (int row = 0; row < c->height; row++) {
          if (c->file->read(rowBuf, bytesPerRow) != bytesPerRow) break;
          pw.beginRow(row);
          for (int col = 0; col < c->width; col++) {
            const uint8_t pv = (rowBuf[col >> 2] >> (6 - (col & 3) * 2)) & 0x03;
            pw.writePixel(col, pv);
          }
        }
        free(rowBuf);

        GUI.drawButtonHints(const_cast<GfxRenderer&>(r), c->labels.btn1, c->labels.btn2, c->labels.btn3,
                            c->labels.btn4);
      },
      &ctx);

  file.close();

  // Sync BW framebuffer state after factory-gray render so onExit's FAST_REFRESH
  // does a correct differential (controller BW state = white, not stale gray planes).
  renderer.clearScreen();
  renderer.cleanupGrayscaleWithFrameBuffer();
}

void PxcViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void PxcViewerActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
}
