#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "String.h"

struct MockESP {
  size_t getFreeHeap() { return 1024 * 1024; }
};
extern MockESP ESP;
inline unsigned long millis() { return 0; }
