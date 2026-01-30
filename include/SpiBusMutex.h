#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace SpiBusMutex {
SemaphoreHandle_t get();
void lock();
void unlock();

struct Guard {
  Guard();
  ~Guard();
  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;
};
}  // namespace SpiBusMutex
