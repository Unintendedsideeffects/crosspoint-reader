#pragma once

#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;

constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdTRUE = 1;
constexpr TickType_t portMAX_DELAY = 0xFFFFFFFFu;

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#endif

#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 1
#endif
