#include "ScheduledTaskRunner.h"

#include <HalClock.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"

namespace ScheduledTaskRunner {

static constexpr uint16_t WIFI_TIMEOUT_MS = 15000;
static constexpr uint16_t LOW_BATTERY_THRESHOLD = 10;

static bool connectWifi() {
  WIFI_STORE.loadFromFile();
  const std::string& lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (lastSsid.empty()) {
    LOG_ERR("SCHED", "No saved WiFi network");
    return false;
  }
  const auto* cred = WIFI_STORE.findCredential(lastSsid);
  if (!cred) {
    LOG_ERR("SCHED", "No credentials for %s", lastSsid.c_str());
    return false;
  }

  WiFi.mode(WIFI_STA);
  if (!cred->password.empty()) {
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  } else {
    WiFi.begin(cred->ssid.c_str());
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("SCHED", "WiFi connect timeout");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }
  LOG_INF("SCHED", "WiFi connected to %s", lastSsid.c_str());
  return true;
}

static void taskNtpSync() {
  LOG_INF("SCHED", "Running NTP sync task");
  if (HalClock::syncNtp()) {
    LOG_INF("SCHED", "NTP sync successful");
  } else {
    LOG_ERR("SCHED", "NTP sync failed");
  }
}

static void taskDownloadSleepImage() {
  if (SETTINGS.scheduledWakeImageUrl[0] == '\0') {
    LOG_DBG("SCHED", "No sleep image URL configured, skipping");
    return;
  }
  LOG_INF("SCHED", "Downloading sleep image from %s", SETTINGS.scheduledWakeImageUrl);

  const std::string tempPath = "/.crosspoint/sleep_download.tmp";
  const std::string destPath = "/sleep.bmp";

  auto result = HttpDownloader::downloadToFile(SETTINGS.scheduledWakeImageUrl, tempPath);

  if (result == HttpDownloader::OK) {
    if (Storage.exists(destPath.c_str())) {
      Storage.remove(destPath.c_str());
    }
    if (Storage.rename(tempPath.c_str(), destPath.c_str())) {
      LOG_INF("SCHED", "Sleep image saved to %s", destPath.c_str());
    } else {
      LOG_ERR("SCHED", "Failed to rename temp file to %s", destPath.c_str());
      Storage.remove(tempPath.c_str());
    }
  } else {
    LOG_ERR("SCHED", "Sleep image download failed (error %d)", result);
    Storage.remove(tempPath.c_str());
  }
}

void run() {
  HalPowerManager::Lock powerLock;

  // Skip tasks if battery is critically low
  uint16_t battery = powerManager.getBatteryPercentage();
  if (battery > 0 && battery < LOW_BATTERY_THRESHOLD) {
    LOG_INF("SCHED", "Battery critically low (%u%%), skipping scheduled tasks", battery);
    return;
  }

  bool needWifi = SETTINGS.scheduledWakeTaskNtp || SETTINGS.scheduledWakeTaskImg;
  bool wifiConnected = false;

  if (needWifi) {
    wifiConnected = connectWifi();
    if (!wifiConnected) {
      LOG_ERR("SCHED", "WiFi required but failed to connect, aborting tasks");
      return;
    }
  }

  if (SETTINGS.scheduledWakeTaskNtp) {
    taskNtpSync();
  }

  if (SETTINGS.scheduledWakeTaskImg) {
    taskDownloadSleepImage();
  }

  if (wifiConnected) {
    HalClock::wifiOff(true);  // skip opportunistic NTP since we already synced
  }

  LOG_INF("SCHED", "Scheduled tasks completed");
}

}  // namespace ScheduledTaskRunner
