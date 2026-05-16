# Flash Size Optimization

This document explains the build configuration choices made to reduce firmware flash size.

**Baseline**: ~6.03 MB (`default` env, before optimizations)

---

## `lib_ignore` — unused Arduino framework libraries

Set in `[base]` in `platformio.ini`. Prevents these Arduino wrapper libraries from being compiled and linked:

| Library | Why safe to ignore |
|---|---|
| `BLE`, `BluetoothSerial`, `SimpleBLE` | No Bluetooth used |
| `WiFiProv` | No Wi-Fi provisioning / SmartConfig UI |
| `ESP_NOW` | Not used |
| `ESP_I2S` | Not used |
| `ESP_SR` | Espressif speech recognition — not used |
| `OpenThread` | Thread protocol — not used |
| `Matter` | Matter/HomeKit — not used |
| `RainMaker` | Espressif cloud — not used |
| `Zigbee` | Not used |
| `Insights` | Espressif diagnostics cloud — not used |
| `TFLiteMicro` | TensorFlow Lite — not used |
| `NetBIOS` | Not used |
| `SPIFFS` | Using SD card only, not SPIFFS |
| `FFat` | FAT on internal flash — not used |
| `USB` | USB stack not needed (CDC-on-boot handled separately) |

Estimated savings: moderate (prevents dead code compilation, may reduce component pulls).

---

## `custom_component_remove` — remove ESP-IDF components

Removes entire IDF components from the build. pioarduino rebuilds the component graph on the first build after this changes.

| Component | Why removed |
|---|---|
| `espressif/network_provisioning` | Wi-Fi provisioning stack |
| `espressif/esp-zboss-lib` | Zigbee stack |
| `espressif/esp-zigbee-lib` | Zigbee API |
| `espressif/esp_rainmaker` | Espressif cloud platform |
| `espressif/rmaker_common` | RainMaker common libs |
| `espressif/esp_insights` | Remote diagnostics |
| `espressif/libsodium` | Cryptography library (not used) |
| `espressif/esp_diag_data_store` | Diagnostics storage |
| `espressif/esp_diagnostics` | Diagnostics framework |
| `espressif/esp-sr` | Speech recognition |
| `espressif/esp-modbus` | Modbus protocol |

---

## `custom_sdkconfig` — ESP-IDF Kconfig overrides

These are applied on top of the base sdkconfig that pioarduino fetches. A change here triggers a full framework rebuild (first build will be slow).

| Option | Effect |
|---|---|
| `# CONFIG_BT_ENABLED is not set` | Disables BT stack at IDF level |
| `# CONFIG_BT_NIMBLE_ENABLED is not set` | Disables NimBLE (BLE host stack) |
| `# CONFIG_BT_CONTROLLER_ENABLED is not set` | Disables BT controller |
| `CONFIG_BT_CONTROLLER_DISABLED=y` | Explicitly marks controller as disabled |

The BT/BLE libraries (`libbt.a` ~3.8 MB archive, `libbtdm_app.a` ~1.3 MB) are pulled in unconditionally by the Arduino HAL (`esp32-hal-bt.c`) unless `CONFIG_BLUEDROID_ENABLED` and `CONFIG_NIMBLE_ENABLED` are both unset. These sdkconfig entries achieve that.

**Note**: The base sdkconfig pioarduino uses is derived from the Tasmota configuration (`sdkconfig_tasmota_esp32`). It also sets `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y` (common CA bundle ~45 KB instead of full Mozilla bundle ~150 KB). This is acceptable for OTA updates since GitHub's certificates are covered by the common bundle.

---

## `sdkconfig.defaults`

After the first build with `custom_sdkconfig` set, pioarduino expands `sdkconfig.defaults` into a full resolved IDF configuration file (~111 KB). This file is managed by the build system — do not hand-edit it. To change Kconfig options, use `custom_sdkconfig` in `platformio.ini` instead.

The original hand-maintained entries (mBedTLS buffer sizes, dynamic allocation) were:

```
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
CONFIG_MBEDTLS_DYNAMIC_BUFFER_ALLOCATION=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
```

These are preserved in the expanded file.

---

## What was NOT changed

- **Full CA certificate bundle**: `OtaUpdater.cpp` uses `esp_crt_bundle_attach` for HTTPS OTA. Switching to a reduced bundle risks CA mismatches for future update servers. Left at default.
- **LTO**: Link-time optimization (`-flto`) can yield 5–15% savings but risks instability with some IDF components. Not enabled.
- **mDNS**: Used by `CalibreConnectActivity` and `CrossPointWebServerActivity` for `.local` hostname resolution. Cannot be removed without breaking those features.
- **SmartConfig** (`libsmartconfig.a`): Pulled in unconditionally by `WiFiSTA.cpp` in the Arduino framework — cannot be excluded without patching the framework.
