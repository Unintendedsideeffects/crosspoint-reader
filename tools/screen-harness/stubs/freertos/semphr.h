#pragma once

#include "FreeRTOS.h"

using SemaphoreHandle_t = void*;

inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<SemaphoreHandle_t>(1); }

inline void vSemaphoreDelete(SemaphoreHandle_t /*sem*/) {}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t /*sem*/, TickType_t /*ticks*/) { return pdTRUE; }

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t /*sem*/) { return pdTRUE; }
