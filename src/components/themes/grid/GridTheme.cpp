#include "GridTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int iconSize = 32;
constexpr int maxRecentListItems = 5;
}  // namespace

Rect GridTheme::getCellRect(Rect r, int col, int row, int colSpan, int rowSpan) const {
  const int totalGapX = (gridColumns - 1) * gridGap;
  const int totalGapY = (gridRows - 1) * gridGap;
  const int cellW = (r.width - 2 * GridMetrics::values.contentSidePadding - totalGapX) / gridColumns;
  const int cellH = (r.height - totalGapY) / gridRows;
  const int x = r.x + GridMetrics::values.contentSidePadding + col * (cellW + gridGap);
  const int y = r.y + row * (cellH + gridGap);
  const int w = cellW * colSpan + gridGap * (colSpan - 1);
  const int h = cellH * rowSpan + gridGap * (rowSpan - 1);
  return Rect{x, y, w, h};
}

// Navigation order (column-major, skipping the free cell):
// 0: Cover (left col, rows 0-1)
// 1: Browse Files (left col, row 2)
// 2: OPDS Browser (left col, row 3) -- only if hasOpdsUrl
// 3: Recent Files (right col, row 0)
// 4: File Transfer (right col, row 2)
// 5: Settings (right col, row 3)

int GridTheme::getGridHomeItemCount(bool hasRecentBook, bool hasOpdsUrl) const {
  int count = 5;  // Cover, Browse, Recents, Transfer, Settings
  if (hasOpdsUrl) {
    count++;  // OPDS Browser
  }
  return count;
}

void GridTheme::drawGridHome(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                             int selectorIndex, bool hasOpdsUrl, bool& coverRendered, bool& coverBufferStored,
                             bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  // Map selectorIndex to grid cell
  // Items in order: Cover(0), Browse(1), OPDS(2 if present), Recents, Transfer, Settings
  int idx = 0;
  const int coverIdx = idx++;
  const int browseIdx = idx++;
  const int opdsIdx = hasOpdsUrl ? idx++ : -1;
  const int recentsIdx = idx++;
  const int transferIdx = idx++;
  const int settingsIdx = idx;

  // Left column
  drawCoverCell(renderer, getCellRect(rect, 0, 0, 1, 2), recentBooks, selectorIndex == coverIdx, coverRendered,
                coverBufferStored, bufferRestored, storeCoverBuffer);

  drawMenuCell(renderer, getCellRect(rect, 0, 2), tr(STR_BROWSE_FILES), Folder, selectorIndex == browseIdx);

  if (hasOpdsUrl) {
    drawMenuCell(renderer, getCellRect(rect, 0, 3), tr(STR_OPDS_BROWSER), Library, selectorIndex == opdsIdx);
  } else {
    drawEmptyCell(renderer, getCellRect(rect, 0, 3));
  }

  // Right column
  drawRecentListCell(renderer, getCellRect(rect, 1, 0), recentBooks, selectorIndex == recentsIdx);

  drawEmptyCell(renderer, getCellRect(rect, 1, 1));

  drawMenuCell(renderer, getCellRect(rect, 1, 2), tr(STR_FILE_TRANSFER), Transfer, selectorIndex == transferIdx);

  drawMenuCell(renderer, getCellRect(rect, 1, 3), tr(STR_SETTINGS_TITLE), Settings, selectorIndex == settingsIdx);
}

void GridTheme::drawCoverCell(GfxRenderer& renderer, Rect cellRect, const std::vector<RecentBook>& recentBooks,
                              bool selected, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                              std::function<bool()> storeCoverBuffer) const {
  if (selected) {
    renderer.fillRoundedRect(cellRect.x, cellRect.y, cellRect.width, cellRect.height, gridCornerRadius,
                             Color::LightGray);
  }

  renderer.drawRoundedRect(cellRect.x, cellRect.y, cellRect.width, cellRect.height, 1, gridCornerRadius, true);

  if (recentBooks.empty()) {
    // No book -- show placeholder
    const int textY = cellRect.y + cellRect.height / 2 - renderer.getLineHeight(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID, cellRect.x + gridPadding, textY, tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, cellRect.x + gridPadding, textY + renderer.getLineHeight(UI_12_FONT_ID) + 4,
                      tr(STR_START_READING), true);
    return;
  }

  const RecentBook& book = recentBooks[0];
  const int coverAreaWidth = cellRect.width - 2 * gridPadding;
  const int coverAreaHeight = cellRect.height - 2 * gridPadding;

  if (!coverRendered) {
    std::string coverPath = book.coverBmpPath;
    bool hasCover = false;

    if (!coverPath.empty()) {
      // Use the cell height minus padding for cover thumbnail
      const int coverHeight = coverAreaHeight;
      const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, coverHeight);

      FsFile file;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          int bmpW = bitmap.getWidth();
          int bmpH = bitmap.getHeight();
          // Scale to fit within the cell, preserving aspect ratio
          float scale = std::min(static_cast<float>(coverAreaWidth) / bmpW, static_cast<float>(coverAreaHeight) / bmpH);
          int drawW = static_cast<int>(bmpW * scale);
          int drawH = static_cast<int>(bmpH * scale);
          int drawX = cellRect.x + (cellRect.width - drawW) / 2;
          int drawY = cellRect.y + (cellRect.height - drawH) / 2;
          renderer.drawBitmap(bitmap, drawX, drawY, drawW, drawH);
          hasCover = true;
        }
        file.close();
      }
    }

    if (!hasCover) {
      // Draw title and author as fallback
      renderer.drawIcon(CoverIcon, cellRect.x + gridPadding, cellRect.y + gridPadding, 32, 32);
      int textWidth = coverAreaWidth;
      auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, 4, EpdFontFamily::BOLD);
      int titleY = cellRect.y + gridPadding + 40;
      for (const auto& line : titleLines) {
        renderer.drawText(UI_12_FONT_ID, cellRect.x + gridPadding, titleY, line.c_str(), true, EpdFontFamily::BOLD);
        titleY += renderer.getLineHeight(UI_12_FONT_ID);
      }
      if (!book.author.empty()) {
        auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
        renderer.drawText(UI_10_FONT_ID, cellRect.x + gridPadding, titleY + 4, author.c_str(), true);
      }
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }
}

void GridTheme::drawMenuCell(GfxRenderer& renderer, Rect cellRect, const char* label, UIIcon icon, bool selected) const {
  if (selected) {
    renderer.fillRoundedRect(cellRect.x, cellRect.y, cellRect.width, cellRect.height, gridCornerRadius,
                             Color::LightGray);
  }

  renderer.drawRoundedRect(cellRect.x, cellRect.y, cellRect.width, cellRect.height, 1, gridCornerRadius, true);

  // Center icon and label vertically
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int totalHeight = iconSize + 4 + lineHeight;
  const int startY = cellRect.y + (cellRect.height - totalHeight) / 2;

  // Draw icon centered horizontally
  const uint8_t* iconBitmap = nullptr;
  switch (icon) {
    case UIIcon::Folder:
      iconBitmap = FolderIcon;
      break;
    case UIIcon::Library:
      iconBitmap = LibraryIcon;
      break;
    case UIIcon::Transfer:
      iconBitmap = TransferIcon;
      break;
    case UIIcon::Settings:
      iconBitmap = Settings2Icon;
      break;
    case UIIcon::Recent:
      iconBitmap = RecentIcon;
      break;
    default:
      break;
  }

  if (iconBitmap) {
    renderer.drawIcon(iconBitmap, cellRect.x + (cellRect.width - iconSize) / 2, startY, iconSize, iconSize);
  }

  // Draw label centered horizontally below icon
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
  const int textX = cellRect.x + (cellRect.width - textWidth) / 2;
  renderer.drawText(UI_12_FONT_ID, textX, startY + iconSize + 4, label, true);
}

void GridTheme::drawRecentListCell(GfxRenderer& renderer, Rect cellRect, const std::vector<RecentBook>& recentBooks,
                                   bool selected) const {
  if (selected) {
    renderer.fillRoundedRect(cellRect.x, cellRect.y, cellRect.width, cellRect.height, gridCornerRadius,
                             Color::LightGray);
  }

  renderer.drawRoundedRect(cellRect.x, cellRect.y, cellRect.width, cellRect.height, 1, gridCornerRadius, true);

  // Header
  const int headerY = cellRect.y + gridPadding;
  renderer.drawText(SMALL_FONT_ID, cellRect.x + gridPadding, headerY, tr(STR_MENU_RECENT_BOOKS), true,
                    EpdFontFamily::BOLD);

  const int listStartY = headerY + renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const int textWidth = cellRect.width - 2 * gridPadding;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);

  if (recentBooks.empty()) {
    renderer.drawText(SMALL_FONT_ID, cellRect.x + gridPadding, listStartY, tr(STR_NO_OPEN_BOOK), true);
    return;
  }

  int maxItems = std::min(static_cast<int>(recentBooks.size()), maxRecentListItems);
  int currentY = listStartY;

  for (int i = 0; i < maxItems && currentY + lineHeight < cellRect.y + cellRect.height - gridPadding; i++) {
    auto title = renderer.truncatedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), textWidth);
    renderer.drawText(SMALL_FONT_ID, cellRect.x + gridPadding, currentY, title.c_str(), true);
    currentY += lineHeight + 2;
  }
}

void GridTheme::drawEmptyCell(GfxRenderer& renderer, Rect cellRect) const {
  renderer.drawRoundedRect(cellRect.x, cellRect.y, cellRect.width, cellRect.height, 1, gridCornerRadius, true);
}
