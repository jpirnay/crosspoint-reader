#pragma once

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update();
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // Virtual button injection (for BLE keyboard)
  void injectButtonPress(uint8_t buttonIndex);
  void injectButtonRelease(uint8_t buttonIndex);

 private:
  HalGPIO& gpio;

  // Virtual button state tracking
  uint8_t virtualButtonState = 0;
  uint8_t virtualButtonPressedEvents = 0;
  uint8_t virtualButtonReleasedEvents = 0;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  bool wasVirtualPressed(uint8_t buttonIndex) const;
  bool wasVirtualReleased(uint8_t buttonIndex) const;
};
