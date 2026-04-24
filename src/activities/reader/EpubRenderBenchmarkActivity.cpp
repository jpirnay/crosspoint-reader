#include "EpubRenderBenchmarkActivity.h"

#include <GfxRenderer.h>

#include "components/UITheme.h"
#include "fontIds.h"

void EpubRenderBenchmarkActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubRenderBenchmarkActivity::loop() {
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) {
    finish();
  }
}

void EpubRenderBenchmarkActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int titleY = contentRect.y + 12;
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, "Render Benchmark", true, EpdFontFamily::BOLD);

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textTop = titleY + renderer.getLineHeight(UI_12_FONT_ID) + 14;
  const int textWidth = contentRect.width - 20;
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, report.c_str(), textWidth, 48);

  int y = textTop;
  for (const auto& line : lines) {
    if (y + lineHeight > contentRect.y + contentRect.height - 30) {
      break;
    }
    renderer.drawText(UI_10_FONT_ID, contentRect.x + 10, y, line.c_str(), true);
    y += lineHeight;
  }

  renderer.drawCenteredText(UI_10_FONT_ID, contentRect.y + contentRect.height - 18, "Press any button to close");
  renderer.displayBuffer();
}
