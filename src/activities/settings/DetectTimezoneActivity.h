#pragma once

#include <string>

#include "activities/Activity.h"

class DetectTimezoneActivity final : public Activity {
 public:
  explicit DetectTimezoneActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DetectTimezone", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { CONNECTING, DETECTING, SUCCESS, FAILED };
  State state = CONNECTING;
  std::string detectedTimezone;
  bool dstKnown = false;
  bool dstActive = false;
  // Set true once WiFi is activated; gates the silent reboot in onExit. Needed
  // because we drop the radio early via esp_wifi_stop() to save power while the
  // result screen is displayed.
  bool wifiActivated = false;

  void onWifiSelectionComplete(bool success);
  void onWifiSelectionCancelled();
  void performDetect();
  const char* dstStatusLabel() const;
};
