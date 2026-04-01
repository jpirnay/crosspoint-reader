#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace GridMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 40,
                                 .headerHeight = 84,
                                 .verticalSpacing = 16,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 40,
                                 .listWithSubtitleRowHeight = 60,
                                 .menuRowHeight = 64,
                                 .menuSpacing = 8,
                                 .tabSpacing = 8,
                                 .tabBarHeight = 40,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 56,
                                 .homeCoverHeight = 226,
                                 .homeCoverTileHeight = 0,
                                 .homeRecentBooksCount = 1,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyWidth = 31,
                                 .keyboardKeyHeight = 50,
                                 .keyboardKeySpacing = 0,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = true};
}

class GridTheme : public LyraTheme {
 public:
  bool usesGridHomeLayout() const override { return true; }
  int getGridHomeItemCount(bool hasRecentBook, bool hasOpdsUrl) const override;
  void drawGridHome(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks, int selectorIndex,
                    bool hasOpdsUrl, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                    std::function<bool()> storeCoverBuffer) const override;

 private:
  // Grid layout: 2 columns x 4 rows
  // Left column:  [Cover (spans rows 0+1)] [Browse Files] [OPDS Browser]
  // Right column: [Recent Files]           [free]         [File Transfer] [Settings]
  static constexpr int gridColumns = 2;
  static constexpr int gridRows = 4;
  static constexpr int gridGap = 8;
  static constexpr int gridCornerRadius = 6;
  static constexpr int gridPadding = 12;

  struct GridCell {
    int col;
    int row;
    int rowSpan;
  };

  Rect getCellRect(Rect contentRect, int col, int row, int colSpan = 1, int rowSpan = 1) const;
  void drawCoverCell(GfxRenderer& renderer, Rect cellRect, const std::vector<RecentBook>& recentBooks, bool selected,
                     bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                     std::function<bool()> storeCoverBuffer) const;
  void drawMenuCell(GfxRenderer& renderer, Rect cellRect, const char* label, UIIcon icon, bool selected) const;
  void drawRecentListCell(GfxRenderer& renderer, Rect cellRect, const std::vector<RecentBook>& recentBooks,
                          bool selected) const;
  void drawEmptyCell(GfxRenderer& renderer, Rect cellRect) const;
};
