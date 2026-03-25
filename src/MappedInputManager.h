#pragma once

#include <HalGPIO.h>

#include <array>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  static constexpr unsigned long DEFAULT_LONG_PRESS_MS = 500;
  static constexpr unsigned long MULTI_CLICK_GAP_MS = 300;

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update();
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  unsigned long getHeldTime(Button button) const;
  bool isHeldLongerThan(Button button, unsigned long durationMs) const;
  bool wasReleasedAtLeast(Button button, unsigned long durationMs) const;
  bool wasReleasedBefore(Button button, unsigned long durationMs) const;
  bool wasLongPressed(Button button) const;
  uint8_t getResolvedClickCount(Button button) const;
  bool wasClicked(Button button) const;
  bool wasDoubleClicked(Button button) const;
  bool wasTripleClicked(Button button) const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

 private:
  struct GestureState {
    unsigned long pressStartedAt = 0;
    unsigned long lastHoldDuration = 0;
    unsigned long clickDeadlineAt = 0;
    uint8_t pendingClickCount = 0;
    uint8_t resolvedClickCount = 0;
    bool longPressReported = false;
    bool longPressEvent = false;
  };

  static constexpr uint8_t PHYSICAL_BUTTON_COUNT = HalGPIO::BTN_POWER + 1;

  HalGPIO& gpio;
  std::array<GestureState, PHYSICAL_BUTTON_COUNT> gestureStates = {};

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  uint8_t mapButtonIndex(Button button) const;
  GestureState& gestureStateFor(uint8_t buttonIndex);
  const GestureState& gestureStateFor(uint8_t buttonIndex) const;
  void updateGestureState();
};
