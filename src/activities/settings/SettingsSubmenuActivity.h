#pragma once
#include <I18n.h>

#include <vector>

#include "activities/Activity.h"
#include "activities/settings/SettingsActivity.h"
#include "util/ButtonNavigator.h"

// Displays a flat list of SettingInfo items launched from a SettingsActivity submenu entry.
// Supports subcategory separators (withSubcategory) exactly as the parent settings tabs do.
class SettingsSubmenuActivity final : public Activity {
  StrId titleId;
  std::vector<SettingInfo> items;
  int selectedIndex = 0;
  int itemCount = 0;
  ButtonNavigator buttonNavigator;

  void toggleItem();

 public:
  explicit SettingsSubmenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, StrId titleId,
                                   std::vector<SettingInfo> items)
      : Activity("SettingsSubmenu", renderer, mappedInput), titleId(titleId), items(std::move(items)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
