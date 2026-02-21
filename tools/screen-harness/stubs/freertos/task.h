#pragma once

#include "FreeRTOS.h"

using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);

enum eNotifyAction { eNoAction = 0, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite };

inline BaseType_t xTaskCreate(TaskFunction_t /*task*/, const char* /*name*/, uint32_t /*stackDepth*/, void* /*params*/,
                              UBaseType_t /*priority*/, TaskHandle_t* createdTask) {
  if (createdTask != nullptr) {
    *createdTask = reinterpret_cast<TaskHandle_t>(1);
  }
  return pdTRUE;
}

inline void vTaskDelete(TaskHandle_t /*task*/) {}

inline void vTaskDelay(TickType_t /*ticks*/) {}

inline uint32_t ulTaskNotifyTake(BaseType_t /*clearCountOnExit*/, TickType_t /*ticksToWait*/) { return 1; }

inline BaseType_t xTaskNotify(TaskHandle_t /*task*/, uint32_t /*value*/, eNotifyAction /*action*/) { return pdTRUE; }

inline TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<TaskHandle_t>(1); }
