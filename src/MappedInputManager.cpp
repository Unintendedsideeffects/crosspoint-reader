#include "MappedInputManager.h"

#include "CrossPointSettings.h"

namespace {
constexpr unsigned long POWER_DOUBLE_TAP_MS = 350;

bool isDualSideLayout() {
  return static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout) ==
         CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT;
}
}  // namespace

decltype(InputManager::BTN_BACK) MappedInputManager::mapButton(const Button button) const {
  const auto frontLayout = static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);
  const auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);

  switch (button) {
    case Button::Back:
      switch (frontLayout) {
        case CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT:
          return InputManager::BTN_BACK;
        case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
          return InputManager::BTN_LEFT;
        case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
          return InputManager::BTN_CONFIRM;
        case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
        default:
          return InputManager::BTN_BACK;
      }
    case Button::Confirm:
      switch (frontLayout) {
        case CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT:
          return InputManager::BTN_CONFIRM;
        case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
          return InputManager::BTN_RIGHT;
        case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
          return InputManager::BTN_LEFT;
        case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
        default:
          return InputManager::BTN_CONFIRM;
      }
    case Button::Left:
      switch (frontLayout) {
        case CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT:
          return InputManager::BTN_LEFT;
        case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
        case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
          return InputManager::BTN_BACK;
        case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
        default:
          return InputManager::BTN_LEFT;
      }
    case Button::Right:
      switch (frontLayout) {
        case CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT:
          return InputManager::BTN_RIGHT;
        case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
          return InputManager::BTN_CONFIRM;
        case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
        case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
        default:
          return InputManager::BTN_RIGHT;
      }
    case Button::Up:
      return InputManager::BTN_UP;
    case Button::Down:
      return InputManager::BTN_DOWN;
    case Button::Power:
      return InputManager::BTN_POWER;
    case Button::PageBack:
      switch (sideLayout) {
        case CrossPointSettings::NEXT_PREV:
          return InputManager::BTN_DOWN;
        case CrossPointSettings::PREV_NEXT:
        default:
          return InputManager::BTN_UP;
      }
    case Button::PageForward:
      switch (sideLayout) {
        case CrossPointSettings::NEXT_PREV:
          return InputManager::BTN_UP;
        case CrossPointSettings::PREV_NEXT:
        default:
          return InputManager::BTN_DOWN;
      }
  }

  return InputManager::BTN_BACK;
}

void MappedInputManager::updatePowerTapState() const {
  if (SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::SELECT) {
    pendingPowerReleaseMs = 0;
    doubleTapReadyMs = 0;
    return;
  }

  const unsigned long now = millis();
  if (doubleTapReadyMs && now - doubleTapReadyMs > POWER_DOUBLE_TAP_MS) {
    doubleTapReadyMs = 0;
  }

  if (!inputManager.wasReleased(InputManager::BTN_POWER)) {
    return;
  }

  if (inputManager.getHeldTime() >= SETTINGS.getPowerButtonDuration()) {
    // Long press detected - clear any pending short-tap state
    pendingPowerReleaseMs = 0;
    doubleTapReadyMs = 0;
    return;
  }

  if (pendingPowerReleaseMs && now - pendingPowerReleaseMs <= POWER_DOUBLE_TAP_MS) {
    pendingPowerReleaseMs = 0;
    doubleTapReadyMs = now;
    return;
  }

  pendingPowerReleaseMs = now;
}

bool MappedInputManager::consumePowerConfirm() const {
  updatePowerTapState();
  if (!pendingPowerReleaseMs || doubleTapReadyMs) {
    return false;
  }
  const unsigned long now = millis();
  if (now - pendingPowerReleaseMs > POWER_DOUBLE_TAP_MS) {
    pendingPowerReleaseMs = 0;
    return true;
  }
  return false;
}

bool MappedInputManager::consumePowerBack() const {
  updatePowerTapState();
  if (!doubleTapReadyMs) {
    return false;
  }
  doubleTapReadyMs = 0;
  return true;
}

bool MappedInputManager::wasPressed(const Button button) const {
  // Note: Power button events are handled in wasReleased() only, since all
  // existing call sites use wasReleased and it matches physical button behavior.
  if (isDualSideLayout()) {
    if (button == Button::Left) {
      return inputManager.wasPressed(InputManager::BTN_BACK) || inputManager.wasPressed(InputManager::BTN_LEFT);
    }
    if (button == Button::Right) {
      return inputManager.wasPressed(InputManager::BTN_CONFIRM) || inputManager.wasPressed(InputManager::BTN_RIGHT);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return inputManager.wasPressed(mapButton(button));
}

bool MappedInputManager::wasReleased(const Button button) const {
  if (button == Button::Confirm && consumePowerConfirm()) {
    return true;
  }
  if (button == Button::Back && consumePowerBack()) {
    return true;
  }
  if (isDualSideLayout()) {
    if (button == Button::Left) {
      return inputManager.wasReleased(InputManager::BTN_BACK) || inputManager.wasReleased(InputManager::BTN_LEFT);
    }
    if (button == Button::Right) {
      return inputManager.wasReleased(InputManager::BTN_CONFIRM) || inputManager.wasReleased(InputManager::BTN_RIGHT);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return inputManager.wasReleased(mapButton(button));
}

bool MappedInputManager::isPressed(const Button button) const {
  if (isDualSideLayout()) {
    if (button == Button::Left) {
      return inputManager.isPressed(InputManager::BTN_BACK) || inputManager.isPressed(InputManager::BTN_LEFT);
    }
    if (button == Button::Right) {
      return inputManager.isPressed(InputManager::BTN_CONFIRM) || inputManager.isPressed(InputManager::BTN_RIGHT);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return inputManager.isPressed(mapButton(button));
}

bool MappedInputManager::wasAnyPressed() const { return inputManager.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return inputManager.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return inputManager.getHeldTime(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  const auto layout = static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);

  switch (layout) {
    case CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT:
      return {previous, previous, next, next};
    case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
      return {previous, next, back, confirm};
    case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
      return {previous, back, confirm, next};
    case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
    default:
      return {back, confirm, previous, next};
  }
}
