#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>

#include "activities/ActivityWithSubactivity.h"

class ValidateSleepImagesActivity final : public ActivityWithSubactivity {
 public:
  explicit ValidateSleepImagesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::function<void()>& goBack)
      : ActivityWithSubactivity("ValidateSleepImages", renderer, mappedInput), goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  enum State { SCANNING, DONE };

  State state = SCANNING;
  TaskHandle_t displayTaskHandle = nullptr;
  std::atomic<bool> exitTaskRequested{false};
  std::atomic<bool> taskHasExited{false};
  std::atomic<bool> updateRequired{false};
  const std::function<void()> goBack;

  int validCount = 0;

  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render();
};
