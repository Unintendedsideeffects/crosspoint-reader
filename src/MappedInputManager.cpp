#include "MappedInputManager.h"

#include <cstring>

#include "CrossPointSettings.h"

namespace {
constexpr unsigned long POWER_DOUBLE_TAP_MS = 350;

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

bool isDualSideLayout() {
  return static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout) ==
         CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT;
}

bool isPowerTapSelectEnabled() {
  return static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.shortPwrBtn) == CrossPointSettings::SELECT;
}

bool equalsLabel(const char* value, const char* expected) {
  return value != nullptr && expected != nullptr && strcmp(value, expected) == 0;
}
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

void MappedInputManager::updatePowerTapState() const {
  if (!isDualSideLayout() && !isPowerTapSelectEnabled()) {
    pendingPowerReleaseMs = 0;
    doubleTapReadyMs = 0;
    return;
  }

  const unsigned long now = millis();
  if (doubleTapReadyMs && now - doubleTapReadyMs > POWER_DOUBLE_TAP_MS) {
    doubleTapReadyMs = 0;
  }

  if (!gpio.wasReleased(HalGPIO::BTN_POWER)) {
    powerReleaseConsumed = false;
    return;
  }

  if (powerReleaseConsumed) {
    return;
  }
  powerReleaseConsumed = true;

  if (gpio.getHeldTime() >= SETTINGS.getPowerButtonDuration()) {
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
  if (button == Button::Confirm && consumePowerConfirm()) {
    return true;
  }
  if (button == Button::Back && consumePowerBack()) {
    return true;
  }
  if (isDualSideLayout()) {
    if (button == Button::Left) {
      return gpio.wasPressed(HalGPIO::BTN_BACK) || gpio.wasPressed(HalGPIO::BTN_LEFT);
    }
    if (button == Button::Right) {
      return gpio.wasPressed(HalGPIO::BTN_CONFIRM) || gpio.wasPressed(HalGPIO::BTN_RIGHT);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return mapButton(button, &HalGPIO::wasPressed);
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
      return gpio.wasReleased(HalGPIO::BTN_BACK) || gpio.wasReleased(HalGPIO::BTN_LEFT);
    }
    if (button == Button::Right) {
      return gpio.wasReleased(HalGPIO::BTN_CONFIRM) || gpio.wasReleased(HalGPIO::BTN_RIGHT);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return mapButton(button, &HalGPIO::wasReleased);
}

bool MappedInputManager::isPressed(const Button button) const {
  if (isDualSideLayout()) {
    if (button == Button::Left) {
      return gpio.isPressed(HalGPIO::BTN_BACK) || gpio.isPressed(HalGPIO::BTN_LEFT);
    }
    if (button == Button::Right) {
      return gpio.isPressed(HalGPIO::BTN_CONFIRM) || gpio.isPressed(HalGPIO::BTN_RIGHT);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return mapButton(button, &HalGPIO::isPressed);
}

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  if (isDualSideLayout()) {
    // In dual-side mode, front buttons map to logical Left/Right.
    // Reword generic vertical hints to match physical behavior.
    const bool verticalHints = equalsLabel(previous, "Up") && equalsLabel(next, "Down");
    const char* dualPrev = verticalHints ? "Left" : previous;
    const char* dualNext = verticalHints ? "Right" : next;
    return {dualPrev, dualPrev, dualNext, dualNext};
  }

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
