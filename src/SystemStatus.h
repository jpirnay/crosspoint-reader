#pragma once

#include <Arduino.h>
#include <HalSpiffs.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include "HalGPIO.h"
#include "HalPowerManager.h"

// Snapshot of device system status, shared between the web server and the
// System Information activity so both surfaces show consistent data.
struct SystemStatus {
  const char* version;
  const char* deviceType;  // "X3" or "X4"
  uint16_t displayWidth;   // Native panel width in pixels (long edge)
  uint16_t displayHeight;  // Native panel height in pixels (short edge)
  std::string chipVersion;
  uint32_t cpuFreqMHz;
  std::string ip;
  std::string wifiMode;  // "STA", "AP", or "Off"
  int rssi;              // dBm; 0 when not in STA mode
  std::string macAddress;
  uint32_t freeHeapBytes;
  uint32_t minFreeHeapBytes;
  uint32_t maxAllocHeapBytes;
  uint64_t flashBytes;             // Total flash chip size
  uint64_t flashAppPartitionSize;  // Size of the running OTA app partition
  uint16_t batteryPercent;
  bool charging;
  uint32_t uptimeSeconds;
  uint64_t sdTotalBytes;
  uint64_t sdUsedBytes;
  uint64_t sdFreeBytes;
  bool spiffsReady;
  uint64_t spiffsTotalBytes;
  uint64_t spiffsUsedBytes;

  static SystemStatus collectFast() {
    SystemStatus s;
    s.version = CROSSPOINT_VERSION;
    if (gpio.deviceIsX3()) {
      s.deviceType = "X3";
      s.displayWidth = 792;
      s.displayHeight = 528;
    } else {
      s.deviceType = "X4";
      s.displayWidth = 800;
      s.displayHeight = 480;
    }
    s.chipVersion = ESP.getChipModel();
    s.chipVersion += " rev ";
    s.chipVersion += std::to_string(ESP.getChipRevision());
    s.cpuFreqMHz = static_cast<uint32_t>(getCpuFrequencyMhz());
    s.freeHeapBytes = ESP.getFreeHeap();
    s.minFreeHeapBytes = ESP.getMinFreeHeap();
    s.maxAllocHeapBytes = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    s.flashBytes = static_cast<uint64_t>(ESP.getFlashChipSize());
    // ESP.getSketchSize() is unreliable on custom partition layouts (the
    // underlying esp_image_verify() call silently fails), so we report the
    // running OTA partition capacity instead — a firm, measurable number.
    const esp_partition_t* running = esp_ota_get_running_partition();
    s.flashAppPartitionSize = running ? static_cast<uint64_t>(running->size) : 0;
    s.batteryPercent = powerManager.getBatteryPercentage();
    s.charging = digitalRead(UART0_RXD) == HIGH;
    s.uptimeSeconds = millis() / 1000;
    s.macAddress = WiFi.macAddress().c_str();
    s.sdTotalBytes = 0;
    s.sdUsedBytes = 0;
    s.sdFreeBytes = 0;
    s.spiffsReady = HalSpiffs::ready();
    s.spiffsTotalBytes = s.spiffsReady ? static_cast<uint64_t>(SPIFFS.totalBytes()) : 0;
    s.spiffsUsedBytes = s.spiffsReady ? static_cast<uint64_t>(SPIFFS.usedBytes()) : 0;

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

  static void fillSdStatus(SystemStatus& s) {
    uint32_t t0 = millis();
    s.sdTotalBytes = Storage.sdTotalBytes();
    s.sdUsedBytes = Storage.sdUsedBytes();
    s.sdFreeBytes = Storage.sdFreeBytes();
    LOG_DBG("SYSINFO", "Filled SD status in %u ms", millis() - t0);
  }

  static SystemStatus collect() {
    SystemStatus s = collectFast();
    fillSdStatus(s);
    return s;
  }
};
