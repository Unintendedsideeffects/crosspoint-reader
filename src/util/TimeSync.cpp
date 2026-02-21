#include "TimeSync.h"

#include <Logging.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ctime>

#include "CrossPointSettings.h"

namespace {
constexpr std::time_t kMinValidTime = 1577836800;  // 2020-01-01 00:00:00 UTC

bool isTimeValid() {
  const std::time_t now = std::time(nullptr);
  return now >= kMinValidTime;
}

bool shouldSync() {
  const auto mode = static_cast<CrossPointSettings::TIME_MODE>(SETTINGS.timeMode);
  if (mode == CrossPointSettings::TIME_MANUAL) {
    return false;
  }
  if (!isTimeValid()) {
    return true;
  }
  if (SETTINGS.lastTimeSyncEpoch == 0) {
    return true;
  }
  const std::time_t now = std::time(nullptr);
  const uint32_t lastSync = SETTINGS.lastTimeSyncEpoch;
  constexpr std::time_t minInterval = 23 * 60 * 60;
  if (now >= static_cast<std::time_t>(lastSync) && now - lastSync < minInterval) {
    return false;
  }
  return true;
}
}  // namespace

namespace TimeSync {
bool syncTimeWithNtpLowMemory() {
  if (!shouldSync()) {
    return isTimeValid();
  }

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  constexpr int maxRetries = 30;  // ~3s
  for (int retry = 0; retry < maxRetries; retry++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      const std::time_t now = std::time(nullptr);
      if (now >= kMinValidTime) {
        SETTINGS.lastTimeSyncEpoch = static_cast<uint32_t>(now);
        if (!SETTINGS.saveToFile()) {
          LOG_WRN("TIMESYNC", "Failed to persist time sync epoch to SD card");
        }
      }
      return isTimeValid();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  if (isTimeValid()) {
    const std::time_t now = std::time(nullptr);
    SETTINGS.lastTimeSyncEpoch = static_cast<uint32_t>(now);
    if (!SETTINGS.saveToFile()) {
      LOG_WRN("TIMESYNC", "Failed to persist time sync epoch to SD card");
    }
    return true;
  }
  return false;
}
}  // namespace TimeSync
