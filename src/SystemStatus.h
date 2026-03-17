#pragma once

#include <Arduino.h>
#include <HalStorage.h>
#include <WiFi.h>

// Snapshot of device system status, shared between the web server and the
// System Information activity so both surfaces show consistent data.
struct SystemStatus {
  const char* version;
  std::string ip;
  std::string wifiMode;  // "STA", "AP", or "Off"
  int rssi;              // dBm; 0 when not in STA mode
  std::string macAddress;
  uint32_t freeHeapBytes;
  uint32_t uptimeSeconds;
  uint64_t sdTotalBytes;
  uint64_t sdUsedBytes;
  uint64_t sdFreeBytes;

  static SystemStatus collect() {
    SystemStatus s;
    s.version = CROSSPOINT_VERSION;
    s.freeHeapBytes = ESP.getFreeHeap();
    s.uptimeSeconds = millis() / 1000;
    s.macAddress = WiFi.macAddress().c_str();
    s.sdTotalBytes = Storage.sdTotalBytes();
    s.sdUsedBytes = Storage.sdUsedBytes();
    s.sdFreeBytes = Storage.sdFreeBytes();

    const wifi_mode_t mode = WiFi.getMode();
    const bool isAP = (mode == WIFI_MODE_AP) || (mode == WIFI_MODE_APSTA);

    if (isAP) {
      s.wifiMode = "AP";
      s.ip = WiFi.softAPIP().toString().c_str();
      s.rssi = 0;
    } else if (WiFi.status() == WL_CONNECTED) {
      s.wifiMode = "STA";
      s.ip = WiFi.localIP().toString().c_str();
      s.rssi = WiFi.RSSI();
    } else {
      s.wifiMode = "Off";
      s.ip = "-";
      s.rssi = 0;
    }

    return s;
  }
};
