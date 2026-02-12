#pragma once

#ifdef ENABLE_BLE_KEYBOARD

#include <Arduino.h>
#include <cstdint>
#include <functional>

/**
 * Singleton class for managing Bluetooth Low Energy (BLE) keyboard connectivity.
 * Handles BLE initialization, advertising, connection management, and HID keyboard input.
 *
 * This class is only compiled when ENABLE_BLE_KEYBOARD is defined.
 * When disabled, zero overhead is incurred.
 */
class BluetoothManager {
 public:
  // Special key enumeration (must be declared before private typedefs that use it)
  enum class SpecialKey { Backspace, Enter, Escape, Tab };

 private:
  static BluetoothManager instance;

  bool initialized = false;
  bool enabled = false;
  bool connected = false;
  unsigned long lastActivityTime = 0;
  unsigned long connectionStartTime = 0;

  // Keyboard state
  uint8_t modifiers = 0;       // Current modifier keys (Ctrl, Shift, Alt, etc.)
  uint8_t lastKeyCode = 0;     // Last key code received
  bool keyProcessed = true;    // Debounce flag

  // Callback for button injection
  using ButtonCallback = std::function<void(uint8_t buttonIndex, bool pressed)>;
  ButtonCallback buttonCallback = nullptr;

  // Callback for character input (for text entry activities)
  using CharCallback = std::function<void(char c)>;
  CharCallback charCallback = nullptr;

  // Callback for special keys (backspace, enter, etc.)
  using SpecialKeyCallback = std::function<void(SpecialKey key)>;
  SpecialKeyCallback specialKeyCallback = nullptr;

  // Private constructor for singleton
  BluetoothManager() = default;

  // Internal methods
  void initializeBLE();
  void startAdvertising();
  void stopAdvertising();
  void handleKeyboardInput(uint8_t modifiers, uint8_t keyCode);
  void mapKeyToButton(uint8_t modifiers, uint8_t keyCode);
  void mapKeyToChar(uint8_t modifiers, uint8_t keyCode);

 public:
  // Delete copy constructor and assignment
  BluetoothManager(const BluetoothManager&) = delete;
  BluetoothManager& operator=(const BluetoothManager&) = delete;

  // Get singleton instance
  static BluetoothManager& getInstance() { return instance; }

  /**
   * Initialize the BLE subsystem. Must be called before enable().
   * Safe to call multiple times.
   */
  void begin();

  /**
   * Enable BLE keyboard functionality and start advertising.
   */
  void enable();

  /**
   * Disable BLE keyboard functionality and disconnect any active connections.
   */
  void disable();

  /**
   * Update method - should be called regularly in main loop.
   * Handles timeouts and connection management.
   */
  void update();

  /**
   * Force disconnect any connected device.
   */
  void disconnect();

  /**
   * Check if BLE is currently enabled.
   */
  bool isEnabled() const { return enabled; }

  /**
   * Check if a device is currently connected.
   */
  bool isConnected() const { return connected; }

  /**
   * Get time since last activity (in milliseconds).
   */
  unsigned long getIdleTime() const;

  /**
   * Get time since connection was established (in milliseconds).
   */
  unsigned long getConnectionTime() const;

  /**
   * Set the callback function for button injection.
   * The callback receives: (buttonIndex, pressed)
   */
  void setButtonCallback(ButtonCallback callback) { buttonCallback = callback; }

  /**
   * Set the callback function for character input (printable characters).
   * The callback receives: (character)
   */
  void setCharCallback(CharCallback callback) { charCallback = callback; }

  /**
   * Set the callback function for special keys (backspace, enter, etc.).
   * The callback receives: (SpecialKey)
   */
  void setSpecialKeyCallback(SpecialKeyCallback callback) { specialKeyCallback = callback; }

  /**
   * Reset activity timer - call this when user interacts with device.
   */
  void resetActivityTimer() { lastActivityTime = millis(); }

  // Connection callbacks (called by NimBLE)
  void onConnect();
  void onDisconnect();
  void onKeyPress(uint8_t modifiers, uint8_t keyCode);
  void onKeyRelease();

  // Constants
  static constexpr unsigned long IDLE_TIMEOUT_MS = 30000;      // 30 seconds
  static constexpr unsigned long ADVERTISING_TIMEOUT_MS = 120000;  // 2 minutes
};

// Helper macro to access Bluetooth manager
#define BT_MANAGER BluetoothManager::getInstance()

#endif  // ENABLE_BLE_KEYBOARD
