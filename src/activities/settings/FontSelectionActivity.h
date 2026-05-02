#pragma once

#include <GfxRenderer.h>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

/// Full-screen list of all reader fonts (built-in + SD card families).
/// Replaces in-place enum cycling for the Reader Font Family setting.
class FontSelectionActivity final : public Activity {
 public:
  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FontSelect", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  uint8_t fontCount = 0;
};
