#include "SpiBusMutex.h"

namespace {
StaticSemaphore_t spiMutexBuffer;

SemaphoreHandle_t createMutex() {
  return xSemaphoreCreateMutexStatic(&spiMutexBuffer);
}
}  // namespace

SemaphoreHandle_t SpiBusMutex::get() {
  static SemaphoreHandle_t spiMutex = createMutex();
  return spiMutex;
}

void SpiBusMutex::lock() {
  auto mutex = get();
  if (mutex) {
    xSemaphoreTake(mutex, portMAX_DELAY);
  }
}

void SpiBusMutex::unlock() {
  auto mutex = get();
  if (mutex) {
    xSemaphoreGive(mutex);
  }
}

SpiBusMutex::Guard::Guard() { lock(); }

SpiBusMutex::Guard::~Guard() { unlock(); }
