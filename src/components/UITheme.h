#pragma once

#include <I18n.h>

#include <functional>
#include <memory>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  UITheme();
  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() const { return *currentMetrics; }
  const BaseTheme& getTheme() const { return *currentTheme; }
  void reload();
  void setTheme(CrossPointSettings::UI_THEME type);
  static int getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle);
  static std::string makeSeparatorTitle(const std::string& title);
  static std::string makeSeparatorTitle(StrId labelId);
  static bool isSeparatorTitle(const std::string& title);
  static std::string stripSeparatorTitle(const std::string& title);
  // Returns a selectable predicate for use with ButtonNavigator::setSelectablePredicate().
  // Items whose title is marked as a separator (via makeSeparatorTitle) are skipped during
  // navigation. Pass the same title getter you pass to drawList so rendering and navigation
  // always agree on which items are separators.
  //
  // Typical usage pattern for a menu with section headers:
  //
  //   struct MenuItem {
  //     Action action; StrId labelId; bool isSeparator = false;
  //     static MenuItem separator(StrId label) { return {Action::NONE, label, true}; }
  //   };
  //   const std::vector<MenuItem> items = buildMenuItems();
  //   // In buildMenuItems, use MenuItem::separator(StrId) for section headers.
  //
  //   // Title getter — used by both drawList and makeSelectablePredicate:
  //   auto titleGetter = [&](int i) {
  //     const auto t = I18N.get(items[i].labelId);
  //     return items[i].isSeparator ? UITheme::makeSeparatorTitle(t) : t;
  //   };
  //
  //   // onEnter: wire navigation so separators are skipped:
  //   const auto pred = UITheme::makeSelectablePredicate(items.size(), titleGetter);
  //   buttonNavigator.setSelectablePredicate(pred, items.size());
  //
  //   // render: pass the same getter to drawList; separator rows are drawn automatically:
  //   GUI.drawList(renderer, rect, items.size(), selectedIndex, titleGetter, ...);
  static std::function<bool(int)> makeSelectablePredicate(int total, std::function<std::string(int)> titleGetter);

  // Returns the drawable content Rect accounting for screen orientation and visible button hints.
  // Bottom hints occupy the physical bottom edge; side hints occupy the physical right edge.
  // The mapping to logical edges is orientation-dependent.
  static Rect getContentRect(const GfxRenderer& renderer, bool hasBottomHints, bool hasSideHints);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  static int getStatusBarHeight();
  static int getProgressBarHeight();

 private:
  const ThemeMetrics* currentMetrics;
  std::unique_ptr<BaseTheme> currentTheme;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
