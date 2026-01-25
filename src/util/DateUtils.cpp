#include "DateUtils.h"

#include <cstdio>
#include <ctime>
#include <string>

#include "CrossPointSettings.h"

namespace DateUtils {
std::string currentDate() {
  std::time_t now = std::time(nullptr);
  if (now <= 0) {
    return {};
  }

  // Treat very old epochs as "time not set".
  constexpr std::time_t kMinValidTime = 1577836800;  // 2020-01-01 00:00:00 UTC
  if (now < kMinValidTime) {
    return {};
  }

  const auto mode = static_cast<CrossPointSettings::TIME_MODE>(SETTINGS.timeMode);
  if (mode == CrossPointSettings::TIME_LOCAL) {
    now += SETTINGS.getTimeZoneOffsetSeconds();
  }

  std::tm timeInfo {};
  if (!gmtime_r(&now, &timeInfo)) {
    return {};
  }

  char buffer[11] = {};
  const int year = timeInfo.tm_year + 1900;
  const int month = timeInfo.tm_mon + 1;
  const int day = timeInfo.tm_mday;
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  return std::string(buffer);
}
}  // namespace DateUtils
