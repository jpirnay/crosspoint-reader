#include "ButtonNavigator.h"

const MappedInputManager* ButtonNavigator::mappedInput = nullptr;

void ButtonNavigator::onNext(const Callback& callback) {
  onNextPress(callback);
  onNextContinuous(callback);
}

void ButtonNavigator::onPrevious(const Callback& callback) {
  onPreviousPress(callback);
  onPreviousContinuous(callback);
}

void ButtonNavigator::onPressAndContinuous(const Buttons& buttons, const Callback& callback) {
  onPress(buttons, callback);
  onContinuous(buttons, callback);
}

void ButtonNavigator::onNextPress(const Callback& callback) { onPress(getNextButtons(), callback); }

void ButtonNavigator::onPreviousPress(const Callback& callback) { onPress(getPreviousButtons(), callback); }

void ButtonNavigator::onNextRelease(const Callback& callback) { onRelease(getNextButtons(), callback); }

void ButtonNavigator::onPreviousRelease(const Callback& callback) { onRelease(getPreviousButtons(), callback); }

void ButtonNavigator::onNextContinuous(const Callback& callback) { onContinuous(getNextButtons(), callback); }

void ButtonNavigator::onPreviousContinuous(const Callback& callback) { onContinuous(getPreviousButtons(), callback); }

void ButtonNavigator::onMenuNext(const StepCallback& callback) {
  if (!mappedInput) return;

  if (mappedInput->wasReleased(MappedInputManager::Button::Right) ||
      mappedInput->wasClicked(MappedInputManager::Button::Down)) {
    callback(1);
  }
}

void ButtonNavigator::onMenuPrevious(const StepCallback& callback) {
  if (!mappedInput) return;

  if (mappedInput->wasReleased(MappedInputManager::Button::Left) ||
      mappedInput->wasClicked(MappedInputManager::Button::Up)) {
    callback(1);
    return;
  }

  if (mappedInput->wasDoubleClicked(MappedInputManager::Button::Up)) {
    callback(5);
  }
}

void ButtonNavigator::onMenuFirst(const Callback& callback) {
  if (mappedInput != nullptr && mappedInput->wasLongPressed(MappedInputManager::Button::Up)) {
    callback();
  }
}

void ButtonNavigator::onMenuLast(const Callback& callback) {
  if (mappedInput != nullptr && mappedInput->wasLongPressed(MappedInputManager::Button::Down)) {
    callback();
  }
}

void ButtonNavigator::onPress(const Buttons& buttons, const Callback& callback) {
  const bool wasPressed = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });

  if (wasPressed) {
    callback();
  }
}

void ButtonNavigator::onRelease(const Buttons& buttons, const Callback& callback) {
  const bool wasReleased = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasReleased(button);
  });

  if (wasReleased) {
    if (lastContinuousNavTime == 0) {
      callback();
    }

    lastContinuousNavTime = 0;
  }
}

void ButtonNavigator::onContinuous(const Buttons& buttons, const Callback& callback) {
  const bool isPressed =
      std::any_of(buttons.begin(), buttons.end(), [this, &buttons](const MappedInputManager::Button button) {
        return mappedInput != nullptr && mappedInput->isPressed(button) && shouldNavigateContinuously(buttons);
      });

  if (isPressed) {
    callback();
    lastContinuousNavTime = millis();
  }
}

bool ButtonNavigator::shouldNavigateContinuously(const Buttons& buttons) const {
  if (!mappedInput) return false;

  const auto heldTime =
      std::accumulate(buttons.begin(), buttons.end(), 0UL,
                      [this](const unsigned long maxHeldTime, const MappedInputManager::Button button) {
                        return std::max(maxHeldTime, mappedInput->getHeldTime(button));
                      });
  const bool buttonHeldLongEnough = heldTime > continuousStartMs;
  const bool navigationIntervalElapsed = (millis() - lastContinuousNavTime) > continuousIntervalMs;

  return buttonHeldLongEnough && navigationIntervalElapsed;
}

int ButtonNavigator::nextIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the next index with wrap-around
  return (currentIndex + 1) % totalItems;
}

int ButtonNavigator::previousIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the previous index with wrap-around
  return (currentIndex + totalItems - 1) % totalItems;
}

int ButtonNavigator::nextIndexBy(const int currentIndex, const int totalItems, const int steps) {
  if (totalItems <= 0 || steps <= 0) return currentIndex;

  return (currentIndex + steps) % totalItems;
}

int ButtonNavigator::previousIndexBy(const int currentIndex, const int totalItems, const int steps) {
  if (totalItems <= 0 || steps <= 0) return currentIndex;

  return (currentIndex + totalItems - (steps % totalItems)) % totalItems;
}

int ButtonNavigator::nextPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return nextIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex < lastPageIndex) {
    return (currentPageIndex + 1) * itemsPerPage;
  }

  return 0;
}

int ButtonNavigator::previousPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return previousIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex > 0) {
    return (currentPageIndex - 1) * itemsPerPage;
  }

  return lastPageIndex * itemsPerPage;
}
