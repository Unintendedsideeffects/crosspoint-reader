#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>

#include "activities/ActivityWithSubactivity.h"

class FactoryResetActivity final : public ActivityWithSubactivity {
 public:
  explicit FactoryResetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::function<void()>& goBack)
      : ActivityWithSubactivity("FactoryReset", renderer, mappedInput), goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  enum State { WARNING, RESETTING, SUCCESS, FAILED };

  State state = WARNING;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::atomic<bool> exitTaskRequested{false};
  std::atomic<bool> taskHasExited{false};
  bool updateRequired = false;
  const std::function<void()> goBack;
  unsigned long restartAtMs = 0;

  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render();
  bool performFactoryReset();
};
