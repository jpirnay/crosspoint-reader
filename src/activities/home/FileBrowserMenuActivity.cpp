#include "FileBrowserMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

FileBrowserMenuActivity::FileBrowserMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const std::string& selectedName, const bool isDirectory,
                                                 const FileBrowserSortOrder currentSort)
    : Activity("FileBrowserMenu", renderer, mappedInput),
      menuItems(buildMenuItems(isDirectory)),
      currentSort(currentSort),
      selectedName(selectedName) {}

std::vector<FileBrowserMenuActivity::MenuItem> FileBrowserMenuActivity::buildMenuItems(const bool isDirectory) {
  std::vector<MenuItem> items;
  items.reserve(8);
  items.push_back({MenuAction::SORT_NAME_ASC, StrId::STR_SORT_NAME_ASC});
  items.push_back({MenuAction::SORT_NAME_DESC, StrId::STR_SORT_NAME_DESC});
  items.push_back({MenuAction::SORT_TITLE_ASC, StrId::STR_SORT_TITLE_ASC});
  items.push_back({MenuAction::SORT_TITLE_DESC, StrId::STR_SORT_TITLE_DESC});
  items.push_back({MenuAction::SORT_DATE_NEWEST, StrId::STR_SORT_DATE_NEWEST});
  items.push_back({MenuAction::SORT_DATE_OLDEST, StrId::STR_SORT_DATE_OLDEST});
  items.push_back({MenuAction::DELETE, StrId::STR_DELETE});
  return items;
}

void FileBrowserMenuActivity::onEnter() {
  Activity::onEnter();
  // Pre-select the currently active sort item
  for (int i = 0; i < static_cast<int>(menuItems.size()); ++i) {
    const auto action = menuItems[i].action;
    if ((currentSort == FileBrowserSortOrder::NAME_ASC && action == MenuAction::SORT_NAME_ASC) ||
        (currentSort == FileBrowserSortOrder::NAME_DESC && action == MenuAction::SORT_NAME_DESC) ||
        (currentSort == FileBrowserSortOrder::TITLE_ASC && action == MenuAction::SORT_TITLE_ASC) ||
        (currentSort == FileBrowserSortOrder::TITLE_DESC && action == MenuAction::SORT_TITLE_DESC) ||
        (currentSort == FileBrowserSortOrder::DATE_NEWEST && action == MenuAction::SORT_DATE_NEWEST) ||
        (currentSort == FileBrowserSortOrder::DATE_OLDEST && action == MenuAction::SORT_DATE_OLDEST)) {
      selectedIndex = i;
      break;
    }
  }
  requestUpdate();
}

void FileBrowserMenuActivity::onExit() { Activity::onExit(); }

void FileBrowserMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(MenuResult{static_cast<int>(menuItems[selectedIndex].action), 0, 0});
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
}

void FileBrowserMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  // Title: show selected file/dir name
  const std::string truncTitle =
      renderer.truncatedText(UI_12_FONT_ID, selectedName.c_str(), pageWidth - 40, EpdFontFamily::BOLD);
  const int titleX = (pageWidth - renderer.getTextWidth(UI_12_FONT_ID, truncTitle.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15, truncTitle.c_str(), true, EpdFontFamily::BOLD);

  // Menu items
  constexpr int startY = 55;
  constexpr int lineHeight = 30;

  for (size_t i = 0; i < menuItems.size(); ++i) {
    const int displayY = startY + static_cast<int>(i) * lineHeight;
    const bool isSelected = (static_cast<int>(i) == selectedIndex);

    if (isSelected) {
      renderer.fillRect(0, displayY, pageWidth - 1, lineHeight, true);
    }

    renderer.drawText(UI_10_FONT_ID, 20, displayY, I18N.get(menuItems[i].labelId), !isSelected);

    // Show a marker next to the currently active sort option
    const auto action = menuItems[i].action;
    const bool isActive =
        (currentSort == FileBrowserSortOrder::NAME_ASC && action == MenuAction::SORT_NAME_ASC) ||
        (currentSort == FileBrowserSortOrder::NAME_DESC && action == MenuAction::SORT_NAME_DESC) ||
        (currentSort == FileBrowserSortOrder::TITLE_ASC && action == MenuAction::SORT_TITLE_ASC) ||
        (currentSort == FileBrowserSortOrder::TITLE_DESC && action == MenuAction::SORT_TITLE_DESC) ||
        (currentSort == FileBrowserSortOrder::DATE_NEWEST && action == MenuAction::SORT_DATE_NEWEST) ||
        (currentSort == FileBrowserSortOrder::DATE_OLDEST && action == MenuAction::SORT_DATE_OLDEST);

    if (isActive) {
      constexpr auto marker = "*";
      const auto markerWidth = renderer.getTextWidth(UI_10_FONT_ID, marker);
      renderer.drawText(UI_10_FONT_ID, pageWidth - 20 - markerWidth, displayY, marker, !isSelected);
    }
  }

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
