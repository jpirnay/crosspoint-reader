#pragma once

#include <string>

#include "activities/Activity.h"

class EpubRenderBenchmarkActivity final : public Activity {
 public:
  explicit EpubRenderBenchmarkActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string report)
      : Activity("EpubRenderBenchmark", renderer, mappedInput), report(std::move(report)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string report;
};
