#ifdef ENABLE_BLE_KEYBOARD

#include "BluetoothManager.h"

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEHIDDevice.h>
#include <InputManager.h>

// HID Report Descriptor for standard keyboard
static const uint8_t hidReportDescriptor[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)

    // Modifier keys
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224)
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute)

    // Reserved byte
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant)

    // LED output report
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x05, 0x08,  //   Usage Page (LEDs)
    0x19, 0x01,  //   Usage Minimum (1)
    0x29, 0x05,  //   Usage Maximum (5)
    0x91, 0x02,  //   Output (Data, Variable, Absolute)

    // LED padding
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output (Constant)

    // Key arrays
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array)

    0xC0  // End Collection
};

// BLE server and HID device pointers
static NimBLEServer* pServer = nullptr;
static NimBLEHIDDevice* pHID = nullptr;
static NimBLECharacteristic* pInputCharacteristic = nullptr;

// Singleton instance
BluetoothManager BluetoothManager::instance;

// Forward declarations for callbacks
class ServerCallbacks;
static ServerCallbacks* serverCallbacks = nullptr;

/**
 * BLE Server callbacks
 */
class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer* pServer) override {
    Serial.printf("[%lu] [BLE] Device connected\n", millis());
    BT_MANAGER.onConnect();
  }

  void onDisconnect(NimBLEServer* pServer) override {
    Serial.printf("[%lu] [BLE] Device disconnected\n", millis());
    BT_MANAGER.onDisconnect();
  }
};

/**
 * HID Input callbacks
 */
class InputCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() >= 2) {
      uint8_t modifiers = value[0];
      uint8_t keyCode = value[2];  // First key in array (skip reserved byte)

      if (keyCode != 0) {
        BT_MANAGER.onKeyPress(modifiers, keyCode);
      } else {
        BT_MANAGER.onKeyRelease();
      }
    }
  }
};

void BluetoothManager::begin() {
  if (initialized) {
    return;
  }

  Serial.printf("[%lu] [BLE] Initializing Bluetooth subsystem\n", millis());

  // Initialize NimBLE
  NimBLEDevice::init("CrossPoint");
  NimBLEDevice::setMTU(23);  // Minimum MTU for HID
  NimBLEDevice::setPower(ESP_PWR_LVL_N0);  // 0 dBm (moderate power)

  initialized = true;
  Serial.printf("[%lu] [BLE] Bluetooth initialized\n", millis());
}

void BluetoothManager::enable() {
  if (!initialized) {
    begin();
  }

  if (enabled) {
    return;
  }

  Serial.printf("[%lu] [BLE] Enabling BLE keyboard\n", millis());

  initializeBLE();
  startAdvertising();

  enabled = true;
  lastActivityTime = millis();
}

void BluetoothManager::disable() {
  if (!enabled) {
    return;
  }

  Serial.printf("[%lu] [BLE] Disabling BLE keyboard\n", millis());

  if (connected) {
    disconnect();
  }

  stopAdvertising();

  // Clean up BLE resources
  if (pServer) {
    NimBLEDevice::deinit(true);
    pServer = nullptr;
    pHID = nullptr;
    pInputCharacteristic = nullptr;
  }

  enabled = false;
  initialized = false;
}

void BluetoothManager::initializeBLE() {
  // Create BLE Server
  pServer = NimBLEDevice::createServer();
  if (!serverCallbacks) {
    serverCallbacks = new ServerCallbacks();
  }
  pServer->setCallbacks(serverCallbacks);

  // Create HID Device
  pHID = new NimBLEHIDDevice(pServer);

  // Set HID info
  pHID->manufacturer()->setValue("CrossPoint");
  pHID->pnp(0x02, 0x05ac, 0x820a, 0x0100);  // USB vendor, product, version
  pHID->hidInfo(0x00, 0x01);                 // Country code, flags

  // Set report map
  pHID->reportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));

  // Create input report characteristic
  pInputCharacteristic = pHID->inputReport(1);  // Report ID 1
  pInputCharacteristic->setCallbacks(new InputCallbacks());

  // Start HID service
  pHID->startServices();

  Serial.printf("[%lu] [BLE] HID keyboard service initialized\n", millis());
}

void BluetoothManager::startAdvertising() {
  if (!pServer) {
    return;
  }

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

  // Add HID service UUID
  pAdvertising->addServiceUUID(pHID->hidService()->getUUID());

  // Set appearance as keyboard
  pAdvertising->setAppearance(0x03C1);  // HID Keyboard

  // Set advertising flags
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // 7.5ms min interval
  pAdvertising->setMaxPreferred(0x12);  // 22.5ms max interval

  pAdvertising->start();
  Serial.printf("[%lu] [BLE] Started advertising\n", millis());
}

void BluetoothManager::stopAdvertising() {
  if (!pServer) {
    return;
  }

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->stop();
  Serial.printf("[%lu] [BLE] Stopped advertising\n", millis());
}

void BluetoothManager::disconnect() {
  if (!pServer || !connected) {
    return;
  }

  Serial.printf("[%lu] [BLE] Disconnecting\n", millis());

  // Get the connection info and disconnect
  // NimBLE uses peer info to get connection handle
  if (pServer->getConnectedCount() > 0) {
    // Get the first connected peer's info
    std::vector<uint16_t> peers = pServer->getPeerDevices();
    if (!peers.empty()) {
      pServer->disconnect(peers[0]);
    }
  }
}

void BluetoothManager::update() {
  if (!enabled) {
    return;
  }

  unsigned long now = millis();

  // Check advertising timeout (if not connected)
  if (!connected) {
    if ((now - lastActivityTime) > ADVERTISING_TIMEOUT_MS) {
      Serial.printf("[%lu] [BLE] Advertising timeout, disabling\n", millis());
      disable();
    }
    return;
  }

  // Check idle timeout (if connected)
  if ((now - lastActivityTime) > IDLE_TIMEOUT_MS) {
    Serial.printf("[%lu] [BLE] Idle timeout, disconnecting\n", millis());
    disconnect();
  }
}

void BluetoothManager::onConnect() {
  connected = true;
  connectionStartTime = millis();
  lastActivityTime = millis();
  stopAdvertising();
}

void BluetoothManager::onDisconnect() {
  connected = false;
  lastActivityTime = millis();

  // Restart advertising if still enabled
  if (enabled) {
    startAdvertising();
  }
}

void BluetoothManager::onKeyPress(uint8_t modifiers, uint8_t keyCode) {
  // Debounce - ignore if same key pressed without release
  if (!keyProcessed && keyCode == lastKeyCode) {
    return;
  }

  Serial.printf("[%lu] [BLE] Key press: mod=0x%02X, key=0x%02X\n", millis(), modifiers, keyCode);

  this->modifiers = modifiers;
  this->lastKeyCode = keyCode;
  this->keyProcessed = false;

  lastActivityTime = millis();

  // Try character mapping first (for text input activities)
  mapKeyToChar(modifiers, keyCode);

  // Also try button mapping (for navigation)
  mapKeyToButton(modifiers, keyCode);
}

void BluetoothManager::onKeyRelease() {
  keyProcessed = true;
  lastKeyCode = 0;
  modifiers = 0;
  lastActivityTime = millis();
}

void BluetoothManager::mapKeyToButton(uint8_t modifiers, uint8_t keyCode) {
  if (!buttonCallback) {
    return;
  }

  // Map USB HID key codes to InputManager button indices
  // Reference: https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf

  uint8_t buttonIndex = 0xFF;  // Invalid button

  switch (keyCode) {
    // Arrow keys
    case 0x50:  // Right Arrow
      buttonIndex = InputManager::BTN_RIGHT;
      break;
    case 0x4F:  // Left Arrow
      buttonIndex = InputManager::BTN_LEFT;
      break;
    case 0x52:  // Up Arrow
      buttonIndex = InputManager::BTN_UP;
      break;
    case 0x51:  // Down Arrow
      buttonIndex = InputManager::BTN_DOWN;
      break;

    // Enter/Return
    case 0x28:  // Enter
      buttonIndex = InputManager::BTN_CONFIRM;
      break;

    // Escape/Backspace
    case 0x29:  // Escape
    case 0x2A:  // Backspace
      buttonIndex = InputManager::BTN_BACK;
      break;

    // Page navigation
    case 0x4B:  // Page Up
      if (modifiers == 0) {  // No modifiers
        // Map to custom page back if available
        // For now, map to UP
        buttonIndex = InputManager::BTN_UP;
      }
      break;
    case 0x4E:  // Page Down
      if (modifiers == 0) {  // No modifiers
        // Map to custom page forward if available
        // For now, map to DOWN
        buttonIndex = InputManager::BTN_DOWN;
      }
      break;

    // Space (could be used for page forward)
    case 0x2C:  // Space
      buttonIndex = InputManager::BTN_CONFIRM;
      break;

    default:
      // Unmapped key
      Serial.printf("[%lu] [BLE] Unmapped key: 0x%02X\n", millis(), keyCode);
      return;
  }

  if (buttonIndex != 0xFF) {
    // Trigger button press callback
    buttonCallback(buttonIndex, true);

    // Auto-release after short delay (simulated press)
    // Note: In production, we might want a timer for this
    delay(50);
    buttonCallback(buttonIndex, false);
  }
}

void BluetoothManager::mapKeyToChar(uint8_t modifiers, uint8_t keyCode) {
  // USB HID keyboard codes to character mapping
  // Reference: https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf

  bool shift = (modifiers & 0x02) || (modifiers & 0x20);  // Left Shift or Right Shift

  // Handle special keys first
  if (specialKeyCallback) {
    switch (keyCode) {
      case 0x2A:  // Backspace
        specialKeyCallback(SpecialKey::Backspace);
        return;
      case 0x28:  // Enter
        specialKeyCallback(SpecialKey::Enter);
        return;
      case 0x29:  // Escape
        specialKeyCallback(SpecialKey::Escape);
        return;
      case 0x2B:  // Tab
        specialKeyCallback(SpecialKey::Tab);
        return;
    }
  }

  // Skip if no character callback registered
  if (!charCallback) {
    return;
  }

  char c = '\0';

  // Letters (a-z / A-Z)
  if (keyCode >= 0x04 && keyCode <= 0x1D) {
    c = shift ? ('A' + (keyCode - 0x04)) : ('a' + (keyCode - 0x04));
  }
  // Numbers and symbols (top row)
  else if (keyCode >= 0x1E && keyCode <= 0x27) {
    const char numRow[] = "1234567890";
    const char numRowShift[] = "!@#$%^&*()";
    c = shift ? numRowShift[keyCode - 0x1E] : numRow[keyCode - 0x1E];
  }
  // Special characters
  else {
    switch (keyCode) {
      case 0x2C:  // Space
        c = ' ';
        break;
      case 0x2D:  // - and _
        c = shift ? '_' : '-';
        break;
      case 0x2E:  // = and +
        c = shift ? '+' : '=';
        break;
      case 0x2F:  // [ and {
        c = shift ? '{' : '[';
        break;
      case 0x30:  // ] and }
        c = shift ? '}' : ']';
        break;
      case 0x31:  // \ and |
        c = shift ? '|' : '\\';
        break;
      case 0x33:  // ; and :
        c = shift ? ':' : ';';
        break;
      case 0x34:  // ' and "
        c = shift ? '"' : '\'';
        break;
      case 0x35:  // ` and ~
        c = shift ? '~' : '`';
        break;
      case 0x36:  // , and <
        c = shift ? '<' : ',';
        break;
      case 0x37:  // . and >
        c = shift ? '>' : '.';
        break;
      case 0x38:  // / and ?
        c = shift ? '?' : '/';
        break;
    }
  }

  // Trigger character callback if we have a valid character
  if (c != '\0') {
    Serial.printf("[%lu] [BLE] Character: '%c'\n", millis(), c);
    charCallback(c);
  }
}

unsigned long BluetoothManager::getIdleTime() const {
  if (!enabled) {
    return 0;
  }
  return millis() - lastActivityTime;
}

unsigned long BluetoothManager::getConnectionTime() const {
  if (!connected) {
    return 0;
  }
  return millis() - connectionStartTime;
}

#endif  // ENABLE_BLE_KEYBOARD
