#include "MappedInputManager.h"

#include "CrossPointSettings.h"

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

void MappedInputManager::update() {
  gpio.update();
  updateGestureState();
}

void MappedInputManager::updateGestureState() {
  const unsigned long now = millis();

  for (uint8_t buttonIndex = 0; buttonIndex < PHYSICAL_BUTTON_COUNT; buttonIndex++) {
    auto& state = gestureStates[buttonIndex];
    state.resolvedClickCount = 0;
    state.longPressEvent = false;

    const bool pressed = gpio.isPressed(buttonIndex);
    const bool pressedEvent = gpio.wasPressed(buttonIndex);
    const bool releasedEvent = gpio.wasReleased(buttonIndex);

    if (state.pendingClickCount > 0 && state.clickDeadlineAt > 0 && now >= state.clickDeadlineAt && pressedEvent) {
      state.resolvedClickCount = state.pendingClickCount;
      state.pendingClickCount = 0;
      state.clickDeadlineAt = 0;
    }

    if (pressedEvent) {
      state.pressStartedAt = now;
      state.lastHoldDuration = 0;
      state.longPressReported = false;
    }

    if (pressed) {
      if (state.pressStartedAt == 0) {
        state.pressStartedAt = now;
      }

      state.lastHoldDuration = now - state.pressStartedAt;
      if (!state.longPressReported && state.lastHoldDuration >= DEFAULT_LONG_PRESS_MS) {
        state.longPressReported = true;
        state.longPressEvent = true;
        state.pendingClickCount = 0;
        state.clickDeadlineAt = 0;
      }
    }

    if (releasedEvent) {
      state.lastHoldDuration = (state.pressStartedAt > 0) ? (now - state.pressStartedAt) : 0;

      if (state.longPressReported) {
        state.pendingClickCount = 0;
        state.clickDeadlineAt = 0;
      } else {
        if (state.pendingClickCount < 3) {
          state.pendingClickCount++;
        }
        state.clickDeadlineAt = now + MULTI_CLICK_GAP_MS;
      }

      state.pressStartedAt = 0;
      state.longPressReported = false;
    }

    if (!pressed && state.pendingClickCount > 0 && state.clickDeadlineAt > 0 && now >= state.clickDeadlineAt) {
      state.resolvedClickCount = state.pendingClickCount;
      state.pendingClickCount = 0;
      state.clickDeadlineAt = 0;
    }
  }
}

uint8_t MappedInputManager::mapButtonIndex(const Button button) const {
  const auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& side = kSideLayouts[sideLayout];

  switch (button) {
    case Button::Back:
      return SETTINGS.frontButtonBack;
    case Button::Confirm:
      return SETTINGS.frontButtonConfirm;
    case Button::Left:
      return SETTINGS.frontButtonLeft;
    case Button::Right:
      return SETTINGS.frontButtonRight;
    case Button::Up:
      return HalGPIO::BTN_UP;
    case Button::Down:
      return HalGPIO::BTN_DOWN;
    case Button::Power:
      return HalGPIO::BTN_POWER;
    case Button::PageBack:
      return side.pageBack;
    case Button::PageForward:
      return side.pageForward;
  }

  return HalGPIO::BTN_BACK;
}

MappedInputManager::GestureState& MappedInputManager::gestureStateFor(const uint8_t buttonIndex) {
  return gestureStates[buttonIndex];
}

const MappedInputManager::GestureState& MappedInputManager::gestureStateFor(const uint8_t buttonIndex) const {
  return gestureStates[buttonIndex];
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  return (gpio.*fn)(mapButtonIndex(button));
}

bool MappedInputManager::wasPressed(const Button button) const { return mapButton(button, &HalGPIO::wasPressed); }

bool MappedInputManager::wasReleased(const Button button) const { return mapButton(button, &HalGPIO::wasReleased); }

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

unsigned long MappedInputManager::getHeldTime(const Button button) const {
  const uint8_t buttonIndex = mapButtonIndex(button);
  const auto& state = gestureStateFor(buttonIndex);

  if (gpio.isPressed(buttonIndex) && state.pressStartedAt > 0) {
    return millis() - state.pressStartedAt;
  }

  return state.lastHoldDuration;
}

bool MappedInputManager::isHeldLongerThan(const Button button, const unsigned long durationMs) const {
  return isPressed(button) && getHeldTime(button) >= durationMs;
}

bool MappedInputManager::wasReleasedAtLeast(const Button button, const unsigned long durationMs) const {
  return wasReleased(button) && getHeldTime(button) >= durationMs;
}

bool MappedInputManager::wasReleasedBefore(const Button button, const unsigned long durationMs) const {
  return wasReleased(button) && getHeldTime(button) < durationMs;
}

bool MappedInputManager::wasLongPressed(const Button button) const {
  return gestureStateFor(mapButtonIndex(button)).longPressEvent;
}

uint8_t MappedInputManager::getResolvedClickCount(const Button button) const {
  return gestureStateFor(mapButtonIndex(button)).resolvedClickCount;
}

bool MappedInputManager::wasClicked(const Button button) const { return getResolvedClickCount(button) == 1; }

bool MappedInputManager::wasDoubleClicked(const Button button) const { return getResolvedClickCount(button) == 2; }

bool MappedInputManager::wasTripleClicked(const Button button) const { return getResolvedClickCount(button) == 3; }

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