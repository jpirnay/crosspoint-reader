# Flash Size Optimization

This document explains the build configuration choices made to reduce firmware flash size, and what was actually measured.

**Baseline**: ~6.03 MB (`default` env, before changes)  
**After changes**: ~6.10 MB (`default` env) — **no net saving**

---

## Results

The optimizations had no measurable effect. The firmware grew slightly (~70 KB), likely due to the expanded sdkconfig bringing in slightly different defaults from the Tasmota base config.

### Why `lib_ignore` didn't help

`lib_ignore` prevents the Arduino *wrapper* libraries (e.g. `BLE`, `OpenThread`) from being compiled. These wrappers are thin C++ shims — the underlying ESP-IDF component libraries (`libbt.a`, `libopenthread.a`, etc.) are pre-compiled into the Arduino framework package and linked regardless. The wrapper code itself contributes negligible flash.

### Why `custom_component_remove` didn't help

`custom_component_remove` works when using the **ESP-IDF framework** (`framework = espidf`). With `framework = arduino`, pioarduino links against pre-built static libraries bundled in `framework-arduinoespressif32-libs`. The component graph is already frozen in those `.a` files — removing components from the IDF CMake graph doesn't recompile the pre-built libs.

### Why `custom_sdkconfig` BT disable didn't help

The `CONFIG_BT_ENABLED=n` sdkconfig entries prevent the BT controller from being *initialized at runtime* and free that RAM, but the `libbt.a` (~3.8 MB archive) and `libbtdm_app.a` (~1.3 MB archive) are still linked into flash. Stripping them would require rebuilding the entire Arduino framework from source — not practical with the pre-built package approach.

---

## What the changes are still good for (runtime RAM)

Even though flash size didn't shrink, the BT sdkconfig entries (`CONFIG_BT_CONTROLLER_DISABLED=y` etc.) prevent the BT controller from reserving its heap memory at boot. This frees ~60–80 KB of RAM at runtime, which is meaningful on the ESP32-C3's 327 KB heap.

---

## Configuration still in place

The following entries remain in `platformio.ini` because they are harmless and document intent clearly. They may become effective if the platform ever switches to a source-build approach.

### `lib_ignore`

| Library | Reason |
|---|---|
| `BLE`, `BluetoothSerial`, `SimpleBLE` | No Bluetooth used |
| `WiFiProv` | No Wi-Fi provisioning |
| `ESP_NOW`, `ESP_I2S`, `ESP_SR` | Not used |
| `OpenThread`, `Matter`, `Zigbee` | Not used |
| `RainMaker`, `Insights` | Espressif cloud — not used |
| `TFLiteMicro` | TensorFlow Lite — not used |
| `NetBIOS`, `FFat` | Not used |
| `SPIFFS` | SD card used instead |
| `USB` | CDC-on-boot handled separately |

### `custom_component_remove`

`espressif/network_provisioning`, `esp-zboss-lib`, `esp-zigbee-lib`, `esp_rainmaker`, `rmaker_common`, `esp_insights`, `libsodium`, `esp_diag_data_store`, `esp_diagnostics`, `esp-sr`, `esp-modbus`

### `custom_sdkconfig`

```
'# CONFIG_BT_ENABLED is not set'
'# CONFIG_BT_NIMBLE_ENABLED is not set'
'# CONFIG_BT_CONTROLLER_ENABLED is not set'
CONFIG_BT_CONTROLLER_DISABLED=y
```

---

## `sdkconfig.defaults`

After the first build with `custom_sdkconfig` set, pioarduino expands `sdkconfig.defaults` into a full resolved IDF configuration file (~111 KB) and overwrites the original hand-maintained file. It is now gitignored and build-system-managed. To change Kconfig options, use `custom_sdkconfig` in `platformio.ini`.

The original hand-maintained mBedTLS entries are preserved in the expanded file:
```
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
CONFIG_MBEDTLS_DYNAMIC_BUFFER_ALLOCATION=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
```

The Tasmota base sdkconfig also sets `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y` (common CA subset instead of full Mozilla bundle). This was left in place — GitHub's OTA endpoints are covered by the common bundle.

---

## What would actually reduce flash

To meaningfully reduce flash with the Arduino framework + pre-built libs, the options are:

- **Switch to `framework = espidf`**: Full source build, `custom_component_remove` works properly. Requires rewriting all Arduino API calls to IDF equivalents — not practical.
- **LTO** (`-flto`): Link-time optimization across compilation units. Could yield 5–15% but risks instability with some IDF components. Not yet tested.
- **Remove `libexpat`**: `XML_GE=0` is already set, minimizing expat. The expat library is used for EPUB parsing — can't be removed.
- **Reduce logging**: The `slim` env already disables serial logging (`-UENABLE_SERIAL_LOG`). No further gains without removing log call sites.
- **SmartConfig** (`libsmartconfig.a`, ~200 KB): Pulled in unconditionally by `WiFiSTA.cpp` in the Arduino framework. Cannot be excluded without patching the framework source.

---

## Side effect: `.dummy/` conflict

When `custom_sdkconfig` or `custom_component_remove` triggers pioarduino's IDF configuration phase, it creates a `.dummy/main.c` stub in the project root containing a bare `app_main()`. On the subsequent Arduino framework build, PlatformIO's source scanner picks this file up and causes a **"multiple definition of `app_main`"** linker error.

**Fix**: delete the `.dummy/` directory after the IDF config phase completes, before the next build. The directory is gitignored.
