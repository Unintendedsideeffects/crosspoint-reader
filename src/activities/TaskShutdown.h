#pragma once

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TaskShutdown {
constexpr int kExitTimeoutMs = 500;
constexpr int kExitPollMs = 10;

inline void requestExit(std::atomic<bool>& exitRequested, std::atomic<bool>& taskHasExited,
                        TaskHandle_t& taskHandle) {
  exitRequested.store(true);
  int waitedMs = 0;
  while (!taskHasExited.load() && waitedMs < kExitTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(kExitPollMs));
    waitedMs += kExitPollMs;
  }
  if (!taskHasExited.load() && taskHandle != nullptr) {
    vTaskDelete(taskHandle);
  }
  taskHandle = nullptr;
}
}  // namespace TaskShutdown
