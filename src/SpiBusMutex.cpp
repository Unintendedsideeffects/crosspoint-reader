#include "SpiBusMutex.h"

namespace {
SemaphoreHandle_t spiMutex = nullptr;
}

SemaphoreHandle_t SpiBusMutex::get() {
  if (!spiMutex) {
    spiMutex = xSemaphoreCreateMutex();
  }
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
