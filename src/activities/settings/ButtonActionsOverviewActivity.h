#pragma once

#include "activities/Activity.h"

class ButtonActionsOverviewActivity final : public Activity {
 public:
  explicit ButtonActionsOverviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ButtonActionsOverview", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
