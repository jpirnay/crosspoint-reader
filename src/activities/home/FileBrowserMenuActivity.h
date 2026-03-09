#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Sort order for the file browser. Persisted in FileBrowserActivity across directory navigations.
enum class FileBrowserSortOrder {
  NAME_ASC,
  NAME_DESC,
  TITLE_ASC,
  TITLE_DESC,
  DATE_NEWEST,
  DATE_OLDEST,
};

class FileBrowserMenuActivity final : public Activity {
 public:
  enum class MenuAction {
    SORT_NAME_ASC,
    SORT_NAME_DESC,
    SORT_TITLE_ASC,
    SORT_TITLE_DESC,
    SORT_DATE_NEWEST,
    SORT_DATE_OLDEST,
    DELETE,
  };

  explicit FileBrowserMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::string& selectedName, bool isDirectory, FileBrowserSortOrder currentSort);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static std::vector<MenuItem> buildMenuItems(bool isDirectory);

  const std::vector<MenuItem> menuItems;
  const FileBrowserSortOrder currentSort;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
  std::string selectedName;
};
