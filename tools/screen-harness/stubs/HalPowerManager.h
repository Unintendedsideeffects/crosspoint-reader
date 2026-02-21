#pragma once

// Screen-harness stub for HalPowerManager.
// Uses angle-bracket includes so the stubs/ versions of dependencies are found
// instead of the relative includes in lib/hal/HalPowerManager.h (which would
// pull in the real HalGPIO.h and cause a class redefinition against the stub).

#include <HalGPIO.h>
#include <freertos/semphr.h>

#include <cassert>
#include <cstdint>

class HalPowerManager;
extern HalPowerManager powerManager;

class HalPowerManager {
 public:
  static constexpr int LOW_POWER_FREQ = 10;
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;

  void begin() {}
  void setPowerSaving(bool /*enabled*/) {}
  void startDeepSleep(HalGPIO& /*gpio*/) const {}
  uint16_t getBatteryPercentage() const { return 100; }

  // RAII lock — no-op in the host build; the screen harness has no scheduler.
  class Lock {
   public:
    explicit Lock() {}
    ~Lock() {}
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};

// Inline definition of the global singleton (C++17).
inline HalPowerManager powerManager;
