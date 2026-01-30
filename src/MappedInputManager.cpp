#include "MappedInputManager.h"

#include "CrossPointSettings.h"

namespace {
constexpr unsigned long POWER_DOUBLE_TAP_MS = 350;

using ButtonIndex = uint8_t;

struct FrontLayoutMap {
  ButtonIndex back;
  ButtonIndex confirm;
  ButtonIndex left;
  ButtonIndex right;
};

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};

// Order matches CrossPointSettings::FRONT_BUTTON_LAYOUT.
constexpr FrontLayoutMap kFrontLayouts[] = {
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT, HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM},
    {HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_BACK, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_RIGHT,
     HalGPIO::BTN_LEFT},  // Index 3: BACK_CONFIRM_RIGHT_LEFT
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT,
     HalGPIO::BTN_RIGHT},  // Index 4: LEFT_LEFT_RIGHT_RIGHT (placeholder)
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
}  // namespace

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto frontLayout = static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);
  const auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& front = kFrontLayouts[frontLayout];
  const auto& side = kSideLayouts[sideLayout];

  switch (button) {
    case Button::Back:
      return (gpio.*fn)(front.back);
    case Button::Confirm:
      return (gpio.*fn)(front.confirm);
    case Button::Left:
      return (gpio.*fn)(front.left);
    case Button::Right:
      return (gpio.*fn)(front.right);
    case Button::Up:
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      return (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      return (gpio.*fn)(side.pageForward);
  }

  return false;
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
  const auto layout = static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);

  switch (layout) {
    case CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT:
      return {previous, previous, next, next};
    case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
      return {previous, next, back, confirm};
    case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
      return {previous, back, confirm, next};
    case CrossPointSettings::BACK_CONFIRM_RIGHT_LEFT:
      return {back, confirm, next, previous};  // Index 3 (Upstream)
    case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
    default:
      return {back, confirm, previous, next};
  }
}
