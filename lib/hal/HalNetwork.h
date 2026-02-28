#pragma once

#include <Arduino.h>

class HalNetwork;
extern HalNetwork network;  // Singleton

/**
 * HAL abstraction for network on/off control.
 *
 * Isolates callers from the underlying network hardware so that:
 *  - The WiFi driver (ESP32 Arduino WiFi) is referenced in one place only.
 *  - Future ports (e.g. X3) or an emulator stub can replace this class
 *    without touching any activity code.
 *
 * Currently wraps ESP32 WiFi STA mode. AP mode and connection management
 * (scanning, credentials, SSID selection) remain in the activity layer,
 * as they are application-level concerns rather than raw hardware control.
 */
class HalNetwork {
 public:
  /**
   * Enable the network interface in STA (station) mode.
   * The caller is responsible for subsequently connecting to an AP via
   * the WiFi library (WiFi.begin) or delegating to WifiSelectionActivity.
   */
  void enable();

  /**
   * Gracefully disconnect and power down the network interface.
   * Callers are responsible for stopping any services that depend on
   * the network (e.g. SNTP, mDNS, HTTP servers) before calling this.
   */
  void disable();

  /**
   * Returns true when the network interface is powered on in any mode
   * (STA, AP, or both).  Used by HalPowerManager to suppress CPU throttling
   * while the radio is active.
   */
  bool isActive() const;

  /**
   * Returns true when the interface is associated to an AP and has a valid
   * IP address.  Prefer this over raw WiFi.status() checks so that the
   * two-condition test (status + valid IP) is applied consistently everywhere.
   */
  bool isConnected() const;

  /**
   * RAII guard: enables the network on construction, disables it on destruction.
   *
   * Prefer this over paired enable()/disable() calls so that the radio is
   * guaranteed to be switched off even on early returns.
   *
   * Short-lived use (single function):
   *   HalNetwork::Guard g(network);
   *
   * Activity lifetime (onEnter / onExit):
   *   std::optional<HalNetwork::Guard> networkGuard_;
   *   networkGuard_.emplace(network);  // onEnter
   *   networkGuard_.reset();           // onExit
   */
  class Guard {
   public:
    explicit Guard(HalNetwork& net) : net_(&net) { net_->enable(); }
    ~Guard() {
      if (net_) net_->disable();
    }
    Guard(Guard&& other) noexcept : net_(other.net_) { other.net_ = nullptr; }
    Guard& operator=(Guard&& other) noexcept {
      if (this != &other) {
        if (net_) net_->disable();
        net_ = other.net_;
        other.net_ = nullptr;
      }
      return *this;
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

   private:
    HalNetwork* net_;
  };
};
