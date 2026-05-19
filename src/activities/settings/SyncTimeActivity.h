#pragma once

#include "activities/Activity.h"

class SyncTimeActivity final : public Activity {
 public:
  explicit SyncTimeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SyncTime", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { CONNECTING, SYNCING, SUCCESS, FAILED };
  State state = CONNECTING;
  time_t preSyncTime = 0;
  time_t prevSyncTime = 0;
  int32_t driftSeconds = 0;
  bool hadTimeBeforeSync = false;
  char syncErrorMsg[64] = {};
  // Set true once WiFi is activated; gates the silent reboot in onExit. Needed
  // because we drop the radio early via esp_wifi_stop() to save power while the
  // result screen is displayed, which makes WiFi.getMode() unreliable.
  bool wifiActivated = false;
  void onWifiSelectionComplete(bool success);
  void onWifiSelectionCancelled();
  void performSync();
};
