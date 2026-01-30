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

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;

 private:
  HalGPIO& gpio;
  mutable unsigned long pendingPowerReleaseMs = 0;
  mutable unsigned long doubleTapReadyMs = 0;
  mutable bool powerReleaseConsumed = false;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  void updatePowerTapState() const;
  bool consumePowerConfirm() const;
  bool consumePowerBack() const;
};