#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "../Activity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

/**
 * Activity for selecting UI language.
 *
 * Shows all known languages (core + non-core).  Selecting a core language
 * activates it immediately.  Selecting a non-core language that is already
 * installed on the SD card activates it immediately.  Selecting a non-core
 * language that is not installed triggers the download flow (TODO).
 */
class LanguageSelectActivity final : public Activity {
 public:
  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LanguageSelect", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();
  bool isInstalled(uint8_t metaIndex) const;

  void onBack() { finish(); }

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  static constexpr uint8_t totalItems = getAllLanguageCount();
};
