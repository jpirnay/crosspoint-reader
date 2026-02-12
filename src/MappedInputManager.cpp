#include "MappedInputManager.h"

#include "CrossPointSettings.h"

#ifdef ENABLE_BLE_KEYBOARD
#include "bluetooth/BluetoothManager.h"
#endif

namespace {
using ButtonIndex = uint8_t;

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};

// Order matches CrossPointSettings::SIDE_BUTTON_LAYOUT.
constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
    {HalGPIO::BTN_DOWN, HalGPIO::BTN_UP},
};
}  // namespace

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& side = kSideLayouts[sideLayout];

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return (gpio.*fn)(side.pageForward);
  }

  return false;
}

bool MappedInputManager::wasPressed(const Button button) const {
  bool physical = mapButton(button, &HalGPIO::wasPressed);

  // Check for virtual button press
  bool virtual_press = false;
  switch (button) {
    case Button::Back:
      virtual_press = wasVirtualPressed(HalGPIO::BTN_BACK);
      break;
    case Button::Confirm:
      virtual_press = wasVirtualPressed(HalGPIO::BTN_CONFIRM);
      break;
    case Button::Left:
      virtual_press = wasVirtualPressed(HalGPIO::BTN_LEFT);
      break;
    case Button::Right:
      virtual_press = wasVirtualPressed(HalGPIO::BTN_RIGHT);
      break;
    case Button::Up:
    case Button::PageBack:
      virtual_press = wasVirtualPressed(HalGPIO::BTN_UP);
      break;
    case Button::Down:
    case Button::PageForward:
      virtual_press = wasVirtualPressed(HalGPIO::BTN_DOWN);
      break;
    default:
      break;
  }

  return physical || virtual_press;
}

bool MappedInputManager::wasReleased(const Button button) const {
  bool physical = mapButton(button, &HalGPIO::wasReleased);

  // Check for virtual button release
  bool virtual_release = false;
  switch (button) {
    case Button::Back:
      virtual_release = wasVirtualReleased(HalGPIO::BTN_BACK);
      break;
    case Button::Confirm:
      virtual_release = wasVirtualReleased(HalGPIO::BTN_CONFIRM);
      break;
    case Button::Left:
      virtual_release = wasVirtualReleased(HalGPIO::BTN_LEFT);
      break;
    case Button::Right:
      virtual_release = wasVirtualReleased(HalGPIO::BTN_RIGHT);
      break;
    case Button::Up:
    case Button::PageBack:
      virtual_release = wasVirtualReleased(HalGPIO::BTN_UP);
      break;
    case Button::Down:
    case Button::PageForward:
      virtual_release = wasVirtualReleased(HalGPIO::BTN_DOWN);
      break;
    default:
      break;
  }

  return physical || virtual_release;
}

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::wasAnyPressed() const {
  return gpio.wasAnyPressed() || (virtualButtonPressedEvents > 0);
}

bool MappedInputManager::wasAnyReleased() const {
  return gpio.wasAnyReleased() || (virtualButtonReleasedEvents > 0);
}

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return back;
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return confirm;
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return previous;
    }
    if (hw == SETTINGS.frontButtonRight) {
      return next;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}

void MappedInputManager::update() {
  // Update physical GPIO
  gpio.update();

  // Clear virtual button events from previous frame
  virtualButtonPressedEvents = 0;
  virtualButtonReleasedEvents = 0;

#ifdef ENABLE_BLE_KEYBOARD
  // Update BLE manager
  if (BT_MANAGER.isEnabled()) {
    BT_MANAGER.update();
  }
#endif
}

void MappedInputManager::injectButtonPress(uint8_t buttonIndex) {
  if (buttonIndex > 6) {
    return;  // Invalid button
  }

  // Set pressed event and state
  virtualButtonPressedEvents |= (1 << buttonIndex);
  virtualButtonState |= (1 << buttonIndex);
}

void MappedInputManager::injectButtonRelease(uint8_t buttonIndex) {
  if (buttonIndex > 6) {
    return;  // Invalid button
  }

  // Set released event and clear state
  virtualButtonReleasedEvents |= (1 << buttonIndex);
  virtualButtonState &= ~(1 << buttonIndex);
}

bool MappedInputManager::wasVirtualPressed(uint8_t buttonIndex) const {
  return virtualButtonPressedEvents & (1 << buttonIndex);
}

bool MappedInputManager::wasVirtualReleased(uint8_t buttonIndex) const {
  return virtualButtonReleasedEvents & (1 << buttonIndex);
}