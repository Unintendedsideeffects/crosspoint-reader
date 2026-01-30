#include "SpiBusMutex.h"

#include <Arduino.h>

SemaphoreHandle_t SpiBusMutex::getMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  if (mutex == NULL) {
    Serial.println("FATAL: Failed to create SPI bus mutex");
    delay(1000);
    ESP.restart();
  }
  return mutex;
}

void SpiBusMutex::take() { xSemaphoreTake(getMutex(), portMAX_DELAY); }

void SpiBusMutex::give() { xSemaphoreGive(getMutex()); }

SpiBusMutex::Guard::Guard() { SpiBusMutex::take(); }

SpiBusMutex::Guard::~Guard() { SpiBusMutex::give(); }
