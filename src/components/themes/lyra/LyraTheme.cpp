#include "LyraTheme.h"

#include <cstdint>
#include <string>

#include "Battery.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"
#include "util/ScreenCoordinateHelper.h"

// Internal constants
constexpr int batteryPercentSpacing = 4;
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
constexpr int topHintButtonY = 345;
constexpr int bottomHintButtonY = 650;

void LyraTheme::drawBattery(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Left aligned battery icon and percentage
  const uint16_t percentage = battery.readPercentage();
  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + batteryPercentSpacing + LyraMetrics::values.batteryWidth, rect.y,
                      percentageText.c_str());
  }
  // 1 column on left, 2 columns on right, 5 columns of battery body
  const int x = rect.x;
  const int y = rect.y + 6;
  const int battWidth = LyraMetrics::values.batteryWidth;

  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rect.height - 1, x + battWidth - 3, y + rect.height - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rect.height - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rect.height - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rect.height - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rect.height - 5);

  // Draw bars
  if (percentage > 10) {
    renderer.fillRect(x + 2, y + 2, 3, rect.height - 4);
  }
  if (percentage > 40) {
    renderer.fillRect(x + 6, y + 2, 3, rect.height - 4);
  }
  if (percentage > 70) {
    renderer.fillRect(x + 10, y + 2, 3, rect.height - 4);
  }
}

void LyraTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  int batteryX = rect.x + rect.width - LyraMetrics::values.contentSidePadding - LyraMetrics::values.batteryWidth;
  if (showBatteryPercentage) {
    const uint16_t percentage = battery.readPercentage();
    const auto percentageText = std::to_string(percentage) + "%";
    batteryX -= renderer.getTextWidth(SMALL_FONT_ID, percentageText.c_str());
  }
  drawBattery(renderer,
              Rect{batteryX, rect.y + 10, LyraMetrics::values.batteryWidth, LyraMetrics::values.batteryHeight},
              showBatteryPercentage);

  if (title) {
    auto truncatedTitle = renderer.truncatedText(
        UI_12_FONT_ID, title, rect.width - LyraMetrics::values.contentSidePadding * 2, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding,
                      rect.y + LyraMetrics::values.batteryBarHeight + 3, truncatedTitle.c_str(), true,
                      EpdFontFamily::BOLD);
    renderer.drawLine(rect.x, rect.y + rect.height - 3, rect.x + rect.width, rect.y + rect.height - 3, 3, true);
  }
}

void LyraTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  int currentX = rect.x + LyraMetrics::values.contentSidePadding;

  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }

  for (const auto& tab : tabs) {
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tab.label, EpdFontFamily::REGULAR);

    if (tab.selected) {
      if (selected) {
        renderer.fillRoundedRect(currentX, rect.y + 1, textWidth + 2 * hPaddingInSelection, rect.height - 4,
                                 cornerRadius, Color::Black);
      } else {
        renderer.fillRectDither(currentX, rect.y, textWidth + 2 * hPaddingInSelection, rect.height - 3,
                                Color::LightGray);
        renderer.drawLine(currentX, rect.y + rect.height - 3, currentX + textWidth + 2 * hPaddingInSelection,
                          rect.y + rect.height - 3, 2, true);
      }
    }

    renderer.drawText(UI_10_FONT_ID, currentX + hPaddingInSelection, rect.y + 6, tab.label, !(tab.selected && selected),
                      EpdFontFamily::REGULAR);

    currentX += textWidth + LyraMetrics::values.tabSpacing + 2 * hPaddingInSelection;
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width, rect.y + rect.height - 1, true);
}

void LyraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<std::string(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? LyraMetrics::values.listWithSubtitleRowHeight : LyraMetrics::values.listRowHeight;
  int pageItems = rect.height / rowHeight;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;

    // Draw scroll bar
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - LyraMetrics::values.scrollBarWidth, scrollBarY, LyraMetrics::values.scrollBarWidth,
                      scrollBarHeight, true);
  }

  // Draw selection
  int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraMetrics::values.scrollBarWidth + LyraMetrics::values.scrollBarRightOffset) : 1);
  if (selectedIndex >= 0) {
    renderer.fillRoundedRect(LyraMetrics::values.contentSidePadding, rect.y + selectedIndex % pageItems * rowHeight,
                             contentWidth - LyraMetrics::values.contentSidePadding * 2, rowHeight, cornerRadius,
                             Color::LightGray);
  }

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;

    // Draw name
    int textWidth = contentWidth - LyraMetrics::values.contentSidePadding * 2 - hPaddingInSelection * 2 -
                    (rowValue != nullptr ? 60 : 0);  // TODO truncate according to value width?
    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection * 2,
                      itemY + 6, item.c_str(), true);

    if (rowSubtitle != nullptr) {
      // Draw subtitle
      std::string subtitleText = rowSubtitle(i);
      auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), textWidth);
      renderer.drawText(SMALL_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection * 2,
                        itemY + 30, subtitle.c_str(), true);
    }

    if (rowValue != nullptr) {
      // Draw value
      std::string valueText = rowValue(i);
      if (!valueText.empty()) {
        const auto valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());

        if (i == selectedIndex) {
          renderer.fillRoundedRect(
              contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection * 2 - valueTextWidth, itemY,
              valueTextWidth + hPaddingInSelection * 2, rowHeight, cornerRadius, Color::Black);
        }

        renderer.drawText(UI_10_FONT_ID,
                          contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection - valueTextWidth,
                          itemY + 6, valueText.c_str(), i != selectedIndex);
      }
    }
  }
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3, const char* btn4) const {
    Serial.printf("[LyraTheme-1] Orientation: %d\n", (int)renderer.getOrientation());
    int logicalW = renderer.getScreenWidth();
    int logicalH = renderer.getScreenHeight();
    Serial.printf("[LyraTheme-1] Logical screen size: %d x %d\n", logicalW, logicalH);
      constexpr int buttonCount = 4;
      constexpr int buttonWidth = 80;
      constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
      constexpr int buttonPositions[buttonCount] = {58, 146, 254, 342};
      constexpr int textYOffset = 7;
      constexpr int cornerRadius = 6;
      constexpr int pw = 480;
      constexpr int ph = 800;
      const char* labels[buttonCount] = {btn1, btn2, btn3, btn4};
      GfxRenderer::Orientation orientation = renderer.getOrientation();
      for (int i = 0; i < buttonCount; ++i) {
        int px = buttonPositions[i];
        int py = ph - buttonHeight;
        int x = 0, y = 0;
        switch (orientation) {
          case GfxRenderer::Portrait:
            x = px;
            y = py;
            break;
          case GfxRenderer::PortraitInverted:
            x = pw - px;
            y = ph - py;
            break;
          case GfxRenderer::LandscapeClockwise:
            x = std::max(ph - py, 0);
            y = std::max(px, pw);
            break;
          case GfxRenderer::LandscapeCounterClockwise:
            x = std::max(py, ph);
            y = std::max(pw - px, 0);
            break;
        }
        Serial.printf("[LyraTheme-1] Btn %d portrait: (%d,%d) -> logical: (%d,%d)\n", i, px, py, x, y);
        if (labels[i] && labels[i][0]) {
          renderer.fillRect(x, y, buttonWidth, buttonHeight, false);
          renderer.drawRoundedRect(x, y, buttonWidth, buttonHeight, 1, cornerRadius, true, true, true, true, true);
          int textW = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
          int textX = x + (buttonWidth - 1 - textW) / 2;
          int textY = y + textYOffset;
          renderer.drawText(SMALL_FONT_ID, textX, textY, labels[i]);
        } else {
          int emptyW = 24;
          int emptyH = 12;
          int emptyX = x + (buttonWidth - emptyW) / 2;
          int emptyY = y + (buttonHeight - emptyH) / 2;
          renderer.drawRoundedRect(emptyX, emptyY, emptyW, emptyH, 1, cornerRadius, true, true, true, true, true);
        }
      }
}

void LyraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
      int logicalW = renderer.getScreenWidth();
      int logicalH = renderer.getScreenHeight();
      constexpr int w = LyraMetrics::values.sideButtonHintsWidth;
      constexpr int screenW = 800; // physical panel width (EInkDisplay::DISPLAY_WIDTH)
      Serial.printf("[LyraTheme-2] Logical screen size: %d x %d\n", logicalW, logicalH);
      Serial.printf("[LyraTheme-2] TopBtn physical: (%d,%d)\n", screenW - w, topHintButtonY);
      Serial.printf("[LyraTheme-2] BotBtn physical: (%d,%d)\n", screenW - w, bottomHintButtonY);
    Serial.printf("[LyraTheme-2] Orientation: %d\n", (int)renderer.getOrientation());
  constexpr int h = 78;
  constexpr int cornerRadius = 6;
  // Top button
  int phyX_top = screenW - w;
  int phyY_top = topHintButtonY;
  int x_top, y_top;
  physicalToLogical(phyX_top, phyY_top, renderer.getOrientation(), &x_top, &y_top);
  Serial.printf("[LyraTheme-Side] TopBtn phy: (%d,%d) -> logical: (%d,%d)\n", phyX_top, phyY_top, x_top, y_top);
  if (topBtn && topBtn[0]) {
    renderer.fillRect(x_top, y_top, w, h, false);
    renderer.drawRoundedRect(x_top, y_top, w, h, 1, cornerRadius, true, true, true, true, true);
    int textW = renderer.getTextWidth(SMALL_FONT_ID, topBtn);
    int textH = renderer.getTextHeight(SMALL_FONT_ID);
    int textX = x_top + (w - 1 - textW) / 2;
    int textY = y_top + (h - textH) / 2 + textH / 2;
    renderer.drawText(SMALL_FONT_ID, textX, textY, topBtn);
  } else {
    int emptyW = 24;
    int emptyH = 12;
    int emptyX = x_top + (w - emptyW) / 2;
    int emptyY = y_top + (h - emptyH) / 2;
    renderer.drawRoundedRect(emptyX, emptyY, emptyW, emptyH, 1, cornerRadius, true, true, true, true, true);
  }
  // Bottom button
  int phyX_bot = screenW - w;
  int phyY_bot = bottomHintButtonY;
  int x_bot, y_bot;
  physicalToLogical(phyX_bot, phyY_bot, renderer.getOrientation(), &x_bot, &y_bot);
  Serial.printf("[LyraTheme] BotBtn phy: (%d,%d) -> logical: (%d,%d)\n", phyX_bot, phyY_bot, x_bot, y_bot);
  if (bottomBtn && bottomBtn[0]) {
    renderer.fillRect(x_bot, y_bot, w, h, false);
    renderer.drawRoundedRect(x_bot, y_bot, w, h, 1, cornerRadius, true, true, true, true, true);
    int textW = renderer.getTextWidth(SMALL_FONT_ID, bottomBtn);
    int textH = renderer.getTextHeight(SMALL_FONT_ID);
    int textX = x_bot + (w - 1 - textW) / 2;
    int textY = y_bot + (h - textH) / 2 + textH / 2;
    renderer.drawText(SMALL_FONT_ID, textX, textY, bottomBtn);
  } else {
    int emptyW = 24;
    int emptyH = 12;
    int emptyX = x_bot + (w - emptyW) / 2;
    int emptyY = y_bot + (h - emptyH) / 2;
    renderer.drawRoundedRect(emptyX, emptyY, emptyW, emptyH, 1, cornerRadius, true, true, true, true, true);
  }
}

void LyraTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = (rect.width - 2 * LyraMetrics::values.contentSidePadding) / 3;
  const int tileHeight = rect.height;
  const int bookTitleHeight = tileHeight - LyraMetrics::values.homeCoverHeight - hPaddingInSelection;
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    if (!coverRendered) {
      for (int i = 0; i < std::min(static_cast<int>(recentBooks.size()), LyraMetrics::values.homeRecentBooksCount);
           i++) {
        std::string coverPath = recentBooks[i].coverBmpPath;
        bool hasCover = true;
        int tileX = LyraMetrics::values.contentSidePadding + tileWidth * i;
        if (coverPath.empty()) {
          hasCover = false;
        } else {
          const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, LyraMetrics::values.homeCoverHeight);

          // First time: load cover from SD and render
          FsFile file;
          if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              float coverHeight = static_cast<float>(bitmap.getHeight());
              float coverWidth = static_cast<float>(bitmap.getWidth());
              float ratio = coverWidth / coverHeight;
              const float tileRatio = static_cast<float>(tileWidth - 2 * hPaddingInSelection) /
                                      static_cast<float>(LyraMetrics::values.homeCoverHeight);
              float cropX = 1.0f - (tileRatio / ratio);

              renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection,
                                  tileWidth - 2 * hPaddingInSelection, LyraMetrics::values.homeCoverHeight, cropX);
            } else {
              hasCover = false;
            }
            file.close();
          }
        }

        if (!hasCover) {
          renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection,
                            tileWidth - 2 * hPaddingInSelection, LyraMetrics::values.homeCoverHeight);
        }
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = true;
    }

    for (int i = 0; i < std::min(static_cast<int>(recentBooks.size()), LyraMetrics::values.homeRecentBooksCount); i++) {
      bool bookSelected = (selectorIndex == i);

      int tileX = LyraMetrics::values.contentSidePadding + tileWidth * i;
      auto title =
          renderer.truncatedText(UI_10_FONT_ID, recentBooks[i].title.c_str(), tileWidth - 2 * hPaddingInSelection);

      if (bookSelected) {
        // Draw selection box
        renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                                 Color::LightGray);
        renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection,
                                LyraMetrics::values.homeCoverHeight, Color::LightGray);
        renderer.fillRectDither(tileX + tileWidth - hPaddingInSelection, tileY + hPaddingInSelection,
                                hPaddingInSelection, LyraMetrics::values.homeCoverHeight, Color::LightGray);
        renderer.fillRoundedRect(tileX, tileY + LyraMetrics::values.homeCoverHeight + hPaddingInSelection, tileWidth,
                                 bookTitleHeight, cornerRadius, false, false, true, true, Color::LightGray);
      }
      renderer.drawText(UI_10_FONT_ID, tileX + hPaddingInSelection,
                        tileY + tileHeight - bookTitleHeight + hPaddingInSelection + 5, title.c_str(), true);
    }
  }
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<std::string(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    int tileWidth = (rect.width - LyraMetrics::values.contentSidePadding * 2 - LyraMetrics::values.menuSpacing) / 2;
    Rect tileRect =
        Rect{rect.x + LyraMetrics::values.contentSidePadding + (LyraMetrics::values.menuSpacing + tileWidth) * (i % 2),
             rect.y + static_cast<int>(i / 2) * (LyraMetrics::values.menuRowHeight + LyraMetrics::values.menuSpacing),
             tileWidth, LyraMetrics::values.menuRowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    const char* label = buttonLabel(i).c_str();
    const int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (LyraMetrics::values.menuRowHeight - lineHeight) / 2;

    // Invert text when the tile is selected, to contrast with the filled background
    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }
}

Rect LyraTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  constexpr int margin = 15;
  constexpr int y = 60;
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + margin * 2;
  const int h = textHeight + margin * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  renderer.fillRect(x - 5, y - 5, w + 10, h + 10, false);
  renderer.drawRect(x, y, w, h, true);

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + margin - 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, true, EpdFontFamily::REGULAR);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}