#include "BookInfoActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Xtc.h>

#include "components/UITheme.h"
#include "fontIds.h"

std::string BookInfoActivity::formatFileSize(const size_t bytes) {
  char buf[16];
  if (bytes < 1024) {
    snprintf(buf, sizeof(buf), "%u B", static_cast<unsigned>(bytes));
  } else if (bytes < 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0f);
  } else {
    snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0f * 1024.0f));
  }
  return buf;
}

void BookInfoActivity::renderLoading() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_INFO));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, contentTop, tr(STR_LOADING));

  renderer.displayBuffer();
}

void BookInfoActivity::loadData() {
  // Get file size
  HalFile f = Storage.open(filePath.c_str());
  if (f) {
    fileSizeBytes = f.fileSize();
    f.close();
  }

  // Load epub metadata — builds cache if missing, which also gives us cover
  if (FsHelpers::hasEpubExtension(filePath)) {
    Epub epub(filePath, "/.crosspoint");
    epub.load(true, true);

    title = epub.getTitle();
    author = epub.getAuthor();
    series = epub.getSeries();
    seriesIndex = epub.getSeriesIndex();
    description = epub.getDescription();

    // Generate thumbnail if not present yet, then record its path
    const auto& metrics = UITheme::getInstance().getMetrics();
    const std::string thumbPath = epub.getThumbBmpPath(metrics.homeCoverHeight);
    if (!Storage.exists(thumbPath.c_str())) {
      epub.generateThumbBmp(metrics.homeCoverHeight);
    }
    coverBmpPath = Storage.exists(thumbPath.c_str()) ? thumbPath : "";
  } else if (FsHelpers::hasXtcExtension(filePath)) {
    Xtc xtc(filePath, "/.crosspoint");
    if (xtc.load()) {
      title = xtc.getTitle();
      author = xtc.getAuthor();

      const auto& metrics = UITheme::getInstance().getMetrics();
      const std::string thumbPath = xtc.getThumbBmpPath(metrics.homeCoverHeight);
      if (!Storage.exists(thumbPath.c_str())) {
        xtc.generateThumbBmp(metrics.homeCoverHeight);
      }
      coverBmpPath = Storage.exists(thumbPath.c_str()) ? thumbPath : "";
    }
  }
}

void BookInfoActivity::onEnter() {
  Activity::onEnter();
  renderLoading();
  loadData();
  requestUpdate(true);
}

void BookInfoActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void BookInfoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_INFO));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;
  const int lineHeightSmall = renderer.getLineHeight(UI_10_FONT_ID);
  const int lineHeightLarge = renderer.getLineHeight(UI_12_FONT_ID);

  // Reserve space so description always gets at least a few lines below the cover block
  const int descReserve = description.empty() ? 0 : (3 * lineHeightSmall + 2 * metrics.verticalSpacing);
  const int topSectionMaxH = contentBottom - contentTop - descReserve;

  // --- Top section: cover (left) + title/author/series (right) ---
  int topSectionBottom = contentTop;
  int metaX = textX;
  int metaWidth = textWidth;

  if (!coverBmpPath.empty()) {
    HalFile coverFile = Storage.open(coverBmpPath.c_str());
    if (coverFile) {
      Bitmap bmp(coverFile);
      if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0) {
        // Natural aspect ratio, capped to available height
        int coverDisplayH = std::min(bmp.getHeight(), topSectionMaxH);
        int coverDisplayW = bmp.getWidth() * coverDisplayH / bmp.getHeight();
        // Also cap width at half the text area
        if (coverDisplayW > textWidth / 2) {
          coverDisplayW = textWidth / 2;
          coverDisplayH = bmp.getHeight() * coverDisplayW / bmp.getWidth();
        }
        renderer.drawBitmap(bmp, textX, contentTop, coverDisplayW, coverDisplayH);
        topSectionBottom = contentTop + coverDisplayH;

        const int gap = metrics.contentSidePadding;
        metaX = textX + coverDisplayW + gap;
        metaWidth = textWidth - coverDisplayW - gap;
      }
      coverFile.close();
    }
  }

  // Title/author/series in right column (or full width when no cover)
  int metaY = contentTop;

  if (!title.empty()) {
    const auto lines = renderer.wrappedText(UI_12_FONT_ID, title.c_str(), metaWidth, 4);
    for (const auto& line : lines) {
      if (metaY + lineHeightLarge > contentBottom) break;
      renderer.drawText(UI_12_FONT_ID, metaX, metaY, line.c_str(), true, EpdFontFamily::BOLD);
      metaY += lineHeightLarge;
    }
    metaY += 4;
  }

  if (!author.empty()) {
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, author.c_str(), metaWidth, 2);
    for (const auto& line : lines) {
      if (metaY + lineHeightSmall > contentBottom) break;
      renderer.drawText(UI_10_FONT_ID, metaX, metaY, line.c_str());
      metaY += lineHeightSmall;
    }
    metaY += 2;
  }

  if (!series.empty()) {
    std::string seriesLine = series;
    if (!seriesIndex.empty()) seriesLine += " #" + seriesIndex;
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, seriesLine.c_str(), metaWidth, 2);
    for (const auto& line : lines) {
      if (metaY + lineHeightSmall > contentBottom) break;
      renderer.drawText(UI_10_FONT_ID, metaX, metaY, line.c_str());
      metaY += lineHeightSmall;
    }
    metaY += 2;
  }

  if (fileSizeBytes > 0) {
    const std::string sizeStr = formatFileSize(fileSizeBytes);
    metaY += metrics.verticalSpacing;
    if (metaY + lineHeightSmall <= contentBottom) {
      renderer.drawText(UI_10_FONT_ID, metaX, metaY, sizeStr.c_str());
      metaY += lineHeightSmall;
    }
  }

  topSectionBottom = std::max(topSectionBottom, metaY);

  // --- Description: full width below the top section ---
  if (!description.empty()) {
    int y = topSectionBottom + metrics.verticalSpacing;
    if (y + lineHeightSmall + 4 < contentBottom) {
      renderer.drawLine(textX, y, pageWidth - metrics.contentSidePadding, y);
      y += 4;

      const int descMaxLines = (contentBottom - y) / lineHeightSmall;
      if (descMaxLines > 0) {
        const auto lines = renderer.wrappedText(UI_10_FONT_ID, description.c_str(), textWidth, descMaxLines);
        for (const auto& line : lines) {
          if (y + lineHeightSmall > contentBottom) break;
          renderer.drawText(UI_10_FONT_ID, textX, y, line.c_str());
          y += lineHeightSmall;
        }
      }
    }
  }

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
