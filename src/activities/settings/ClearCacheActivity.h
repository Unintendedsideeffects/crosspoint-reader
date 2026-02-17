#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>

#include "activities/ActivityWithSubactivity.h"

class ClearCacheActivity final : public ActivityWithSubactivity {
 public:
  explicit ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& goBack)
      : ActivityWithSubactivity("ClearCache", renderer, mappedInput), goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  enum State { WARNING, CLEARING, SUCCESS, FAILED };

  State state = WARNING;
  TaskHandle_t displayTaskHandle = nullptr;
  std::atomic<bool> exitTaskRequested{false};
  std::atomic<bool> taskHasExited{false};
  bool updateRequired = false;
  const std::function<void()> goBack;

  int clearedCount = 0;
  int failedCount = 0;

  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render();
  void clearCache();
};
