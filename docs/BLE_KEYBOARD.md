# Bluetooth Keyboard Support

## Overview

CrossPoint Reader now supports Bluetooth Low Energy (BLE) keyboards, allowing you to navigate and control the e-reader using external keyboards. This feature is designed to be energy-efficient and has zero overhead when disabled.

## Features

- **BLE HID Keyboard Support**: Full HID keyboard input with standard key mapping
- **Automatic Connection Management**: Auto-disconnect after idle timeout to save battery
- **Power Management**: Automatic shutdown on sleep, configurable timeouts
- **Zero Overhead When Disabled**: Complete conditional compilation - no memory or flash usage if not enabled
- **Memory Optimized**: Uses NimBLE library (~15KB RAM, ~70KB Flash) instead of Bluedroid (~60KB RAM, ~500KB Flash)

## Compilation Flags

### Building with BLE Enabled (Default)
```bash
pio run -e default
```
This builds with `ENABLE_BLE_KEYBOARD=1`, including NimBLE library and all Bluetooth features.

### Building without BLE (Zero Overhead)
```bash
pio run -e default_no_ble
```
This builds without BLE support. The `ENABLE_BLE_KEYBOARD` flag is not defined, resulting in:
- **Zero** additional flash usage
- **Zero** additional RAM usage
- All BLE code is completely excluded via `#ifdef` guards

### Custom Build (Manual Control)
To manually control BLE in other environments, modify `platformio.ini`:
```ini
build_flags =
  ${base.build_flags}
  -DENABLE_BLE_KEYBOARD=1    # Enable BLE
  ; or omit this line to disable BLE
```

## Memory Usage

### With BLE Enabled
- **Flash**: ~80-90KB (NimBLE + BluetoothManager implementation)
- **RAM**: ~15-20KB when enabled and connected
  - NimBLE stack: ~10-15KB
  - Connection buffers: ~5KB

### With BLE Disabled
- **Flash**: 0 bytes (completely excluded)
- **RAM**: 0 bytes (completely excluded)

## Power Consumption

BLE keyboard support includes power-saving features to minimize battery impact:

| State | Power Consumption | Notes |
|-------|------------------|-------|
| BLE Disabled (default) | 0mA | No power overhead |
| BLE Enabled, Advertising | ~5-10mA | 2-minute timeout |
| BLE Connected, Active | ~15-30mA | Input being processed |
| BLE Connected, Idle | ~5-10mA | No recent activity |
| Auto-disconnect | 30 seconds | Configurable |

### Power Management Features
1. **Idle Timeout**: Disconnects after 30 seconds of no keyboard activity
2. **Advertising Timeout**: Stops advertising after 2 minutes if no device connects
3. **Sleep Integration**: BLE is automatically disabled when device enters deep sleep
4. **On-Demand**: BLE only initializes when enabled via settings

## Usage

### 1. Enable Bluetooth in Settings
Navigate to: **Settings → System → Bluetooth Keyboard**
- Toggle to **ON**
- Device will start advertising as "CrossPoint"

### 2. Pair Your Keyboard
1. Put your Bluetooth keyboard in pairing mode
2. Search for "CrossPoint" device
3. Connect (no PIN required)

### 3. Key Mapping

The following keys are mapped to CrossPoint buttons:

| Keyboard Key | CrossPoint Button | Function |
|--------------|------------------|----------|
| ↑ Up Arrow | Up Button | Navigate up |
| ↓ Down Arrow | Down Button | Navigate down |
| ← Left Arrow | Left Button | Navigate left / Previous item |
| → Right Arrow | Right Button | Navigate right / Next item |
| Enter / Return | Confirm Button | Select / Confirm |
| Escape | Back Button | Back / Cancel |
| Backspace | Back Button | Back / Cancel |
| Space | Confirm Button | Confirm / Next page |
| Page Up | Up Button | Scroll up (planned: page back) |
| Page Down | Down Button | Scroll down (planned: page forward) |

### 4. Text Input (Keyboard Entry Activity)

When using text input screens (WiFi passwords, OPDS URLs, etc.), you can type directly on your Bluetooth keyboard:

**Supported Characters:**
- All letters (a-z, A-Z) - Shift key works for uppercase
- All numbers (0-9) and symbols (!@#$%^&*() etc.)
- Special characters: `-_=+[]{}\|;:'",<.>/?~`
- Space bar
- Backspace (deletes last character)
- Enter (submits input)
- Escape (cancels input)

**Note:** When a text entry screen is active (KeyboardEntryActivity), the BLE keyboard will:
- Type characters directly into the input field
- Bypass the on-screen virtual keyboard
- Still allow navigation with arrow keys if needed

### 5. Disconnect
- Bluetooth will auto-disconnect after 30 seconds of inactivity
- Manual disconnect: Turn off keyboard or disable BLE in settings
- Device will sleep: BLE automatically disabled

## Implementation Details

### Architecture

```
BLE Keyboard Input Flow:
┌─────────────────┐
│ BLE Keyboard    │
└────────┬────────┘
         │ HID Reports
         ↓
┌─────────────────────────────────────────┐
│ BluetoothManager                        │
│ - NimBLE HID Server                     │
│ - Key Code Processing                   │
│ - Connection Management                 │
└───┬─────────────────────────────────┬───┘
    │                                 │
    │ Button Events                   │ Character Events
    │ (Arrow keys, Enter, Esc)        │ (a-z, 0-9, symbols)
    ↓                                 ↓
┌──────────────────────┐      ┌──────────────────────┐
│ MappedInputManager   │      │ KeyboardEntryActivity│
│ - Virtual Buttons    │      │ - Direct Text Input  │
│ - Merge with Physical│      │ - Special Keys       │
└────────┬─────────────┘      └────────┬─────────────┘
         │                              │
         │ Logical Button Events        │ Text Updates
         ↓                              ↓
┌──────────────────────────────────────────┐
│ Activities                               │
│ - Navigation (buttons)                   │
│ - Text Entry (direct character input)   │
│ - Update UI                              │
└──────────────────────────────────────────┘
```

### Key Files

| File | Purpose |
|------|---------|
| `src/bluetooth/BluetoothManager.h` | BLE manager singleton, HID service, character callbacks |
| `src/bluetooth/BluetoothManager.cpp` | NimBLE initialization, key→button and key→char mapping |
| `src/MappedInputManager.h` | Virtual button injection interface |
| `src/MappedInputManager.cpp` | Merge physical and virtual button inputs |
| `src/activities/util/KeyboardEntryActivity.h` | Text input activity interface |
| `src/activities/util/KeyboardEntryActivity.cpp` | Direct character injection, BLE callback registration |
| `src/main.cpp` | BLE initialization and lifecycle management |
| `src/CrossPointSettings.h` | Bluetooth enabled setting |
| `src/SettingsList.h` | Settings UI integration |

### Conditional Compilation

All BLE code is wrapped in conditional compilation guards:

```cpp
#ifdef ENABLE_BLE_KEYBOARD
  // BLE code here
#endif
```

This ensures:
- ✅ Zero overhead when disabled
- ✅ No library dependencies pulled in
- ✅ No dead code in final binary
- ✅ No RAM allocation

### NimBLE Configuration

NimBLE is configured for minimal memory usage in `platformio.ini`:

```ini
# Reduce maximum connections to save RAM
-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1

# Minimize task stack size
-DCONFIG_BT_NIMBLE_TASK_STACK_SIZE=3072

# Disable debug logs
-DCONFIG_BT_NIMBLE_LOG_LEVEL=0

# Reduce device name length
-DCONFIG_BT_NIMBLE_MAX_DEVICE_NAME_LEN=16

# Disable extended advertising (not needed for HID)
-DCONFIG_BT_NIMBLE_EXT_ADV=0
```

## Troubleshooting

### Keyboard Not Connecting
1. Ensure Bluetooth is enabled in Settings
2. Check keyboard is in pairing mode
3. Restart CrossPoint device
4. Try removing keyboard from device's paired list and re-pair

### High Battery Drain
1. Check if BLE is enabled but not being used
2. Disable Bluetooth in settings when not needed
3. Verify auto-disconnect is working (30s timeout)
4. Consider shorter advertising timeout in code

### Keys Not Working
1. Check key mapping table above
2. Verify keyboard is HID-compliant
3. Check serial logs for key codes: `[BLE] Key press: mod=0xXX, key=0xYY`
4. Custom key mappings can be added in `BluetoothManager::mapKeyToButton()`

### Compilation Errors
1. Ensure PlatformIO is up to date
2. Clean build: `pio run -t clean`
3. Check NimBLE library is installed
4. Verify `ENABLE_BLE_KEYBOARD` flag in your environment

## Future Enhancements

Potential improvements for future versions:

1. **Custom Key Mapping UI**: Allow users to configure key mappings
2. **Multiple Keyboards**: Support connecting multiple input devices
3. **Battery Status Reporting**: Report CrossPoint battery to connected device
4. **Persistent Pairing**: Remember paired devices across reboots
5. **BLE Mouse Support**: Add support for Bluetooth mice
6. **Wake from Sleep**: Wake device when keyboard key pressed (ESP32-C3 limitation)

## Developer Notes

### Adding New Key Mappings

Edit `src/bluetooth/BluetoothManager.cpp`, function `mapKeyToButton()`:

```cpp
void BluetoothManager::mapKeyToButton(uint8_t modifiers, uint8_t keyCode) {
  uint8_t buttonIndex = 0xFF;

  switch (keyCode) {
    case 0xXX:  // Your HID key code
      buttonIndex = InputManager::BTN_YOUR_BUTTON;
      break;
    // ... existing mappings
  }

  if (buttonIndex != 0xFF) {
    buttonCallback(buttonIndex, true);
    delay(50);
    buttonCallback(buttonIndex, false);
  }
}
```

USB HID key codes reference: https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf

### Modifying Timeouts

Edit `src/bluetooth/BluetoothManager.h`:

```cpp
static constexpr unsigned long IDLE_TIMEOUT_MS = 30000;      // Idle disconnect
static constexpr unsigned long ADVERTISING_TIMEOUT_MS = 120000;  // Stop advertising
```

### Debugging

Enable serial logging and monitor for BLE events:

```cpp
[123456] [BLE] Initializing Bluetooth subsystem
[123500] [BLE] HID keyboard service initialized
[123600] [BLE] Started advertising
[125000] [BLE] Device connected
[125100] [BLE] Key press: mod=0x00, key=0x52  # Up Arrow
[155000] [BLE] Idle timeout, disconnecting
```

## Testing Checklist

- [ ] Build with BLE enabled compiles successfully
- [ ] Build without BLE (`default_no_ble`) compiles successfully
- [ ] BLE can be toggled in settings UI
- [ ] Keyboard pairs successfully
- [ ] All mapped keys work correctly
- [ ] Auto-disconnect after idle timeout works
- [ ] Advertising timeout works (2 minutes)
- [ ] BLE disables on sleep
- [ ] BLE re-enables on wake if setting was on
- [ ] No memory leaks during connect/disconnect cycles
- [ ] Battery impact is acceptable
- [ ] Works across all 4 screen orientations

## Credits

- **NimBLE-Arduino**: Apache 2.0 licensed BLE stack by h2zero
- **Implementation**: Based on CrossPoint Reader architecture patterns
- **Inspired by**: Previous failed BLE attempt (referenced by user)

---

**Note**: This feature is currently in development. Please test thoroughly before deploying to production devices.
