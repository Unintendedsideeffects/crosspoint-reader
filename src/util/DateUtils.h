#pragma once

#include <string>

namespace DateUtils {
// Returns current date in YYYY-MM-DD format.
// Returns empty string if system time is not set.
std::string currentDate();
}  // namespace DateUtils
