#include "SpiBusMutex.h"

SemaphoreHandle_t SpiBusMutex::getMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}

void SpiBusMutex::take() { xSemaphoreTake(getMutex(), portMAX_DELAY); }

void SpiBusMutex::give() { xSemaphoreGive(getMutex()); }

SpiBusMutex::Guard::Guard() { SpiBusMutex::take(); }

SpiBusMutex::Guard::~Guard() { SpiBusMutex::give(); }
