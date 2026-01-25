#include "CategorySettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include <cstdio>
#include <ctime>
#include <cstring>
#include <sys/time.h>

#include "CalibreSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "KOReaderSettingsActivity.h"
#include "MappedInputManager.h"
#include "OtaUpdateActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "fontIds.h"

namespace {
std::string formatTimeForInput() {
  const std::time_t now = std::time(nullptr);
  if (now <= 0) {
    return "2024-01-01 00:00";
  }
  std::tm timeInfo {};
  gmtime_r(&now, &timeInfo);
  char buffer[20] = {};
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d", timeInfo.tm_year + 1900, timeInfo.tm_mon + 1,
                timeInfo.tm_mday, timeInfo.tm_hour, timeInfo.tm_min);
  return std::string(buffer);
}

bool parseManualTime(const std::string& text, std::tm& out) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  const int matched = std::sscanf(text.c_str(), "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute);
  if (matched < 3) {
    return false;
  }
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) {
    return false;
  }
  if (matched >= 5 && (hour < 0 || hour > 23 || minute < 0 || minute > 59)) {
    return false;
  }
  out = {};
  out.tm_year = year - 1900;
  out.tm_mon = month - 1;
  out.tm_mday = day;
  out.tm_hour = (matched >= 5) ? hour : 0;
  out.tm_min = (matched >= 5) ? minute : 0;
  out.tm_sec = 0;
  return true;
}
}  // namespace

void CategorySettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<CategorySettingsActivity*>(param);
  self->displayTaskLoop();
}

void CategorySettingsActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();

  selectedSettingIndex = 0;
  updateRequired = true;

  xTaskCreate(&CategorySettingsActivity::taskTrampoline, "CategorySettingsActivityTask", 4096, this, 1,
              &displayTaskHandle);
}

void CategorySettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void CategorySettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Handle actions with early return
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    updateRequired = true;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoBack();
    return;
  }

  // Handle navigation
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedSettingIndex = (selectedSettingIndex > 0) ? (selectedSettingIndex - 1) : (settingsCount - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedSettingIndex = (selectedSettingIndex < settingsCount - 1) ? (selectedSettingIndex + 1) : 0;
    updateRequired = true;
  }
}

void CategorySettingsActivity::toggleCurrentSetting() {
  if (selectedSettingIndex < 0 || selectedSettingIndex >= settingsCount) {
    return;
  }

  const auto& setting = settingsList[selectedSettingIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());

    // When switching to dual-side layout, force power button to SELECT mode
    // (required for Back/Confirm functionality in that layout)
    if (strcmp(setting.name, "Front Button Layout") == 0 &&
        SETTINGS.frontButtonLayout == CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT) {
      SETTINGS.shortPwrBtn = CrossPointSettings::SELECT;
    }

    // Prevent changing power button away from SELECT while in dual-side mode
    // (SELECT is required for Back/Confirm in that layout)
    if (strcmp(setting.name, "Short Power Button Click") == 0 &&
        SETTINGS.frontButtonLayout == CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT) {
      SETTINGS.shortPwrBtn = CrossPointSettings::SELECT;
    }
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    if (strcmp(setting.name, "KOReader Sync") == 0) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new KOReaderSettingsActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    } else if (strcmp(setting.name, "Calibre Settings") == 0) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new CalibreSettingsActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    } else if (strcmp(setting.name, "Clear Cache") == 0) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new ClearCacheActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    } else if (strcmp(setting.name, "Set Manual Time") == 0) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      const std::string prefill = formatTimeForInput();
      enterNewActivity(new KeyboardEntryActivity(
          renderer, mappedInput, "Manual Time (YYYY-MM-DD HH:MM)", prefill, 10,
          16,     // maxLength
          false,  // not password
          [this](const std::string& text) {
            std::tm parsed {};
            if (parseManualTime(text, parsed)) {
              std::time_t localEpoch = std::mktime(&parsed);
              if (localEpoch > 0) {
                int offsetSeconds = 0;
                const auto mode = static_cast<CrossPointSettings::TIME_MODE>(SETTINGS.timeMode);
                if (mode == CrossPointSettings::TIME_LOCAL) {
                  offsetSeconds = SETTINGS.getTimeZoneOffsetSeconds();
                }
                const std::time_t utcEpoch = localEpoch - offsetSeconds;
                timeval tv;
                tv.tv_sec = utcEpoch;
                tv.tv_usec = 0;
                settimeofday(&tv, nullptr);
                SETTINGS.timeMode = CrossPointSettings::TIME_MANUAL;
                SETTINGS.lastTimeSyncEpoch = static_cast<uint32_t>(utcEpoch);
                SETTINGS.saveToFile();
              }
            }
            exitActivity();
            updateRequired = true;
          },
          [this]() {
            exitActivity();
            updateRequired = true;
          }));
      xSemaphoreGive(renderingMutex);
    } else if (strcmp(setting.name, "Check for updates") == 0) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new OtaUpdateActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    }
  } else {
    return;
  }

  SETTINGS.saveToFile();
}

void CategorySettingsActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void CategorySettingsActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, categoryName, true, EpdFontFamily::BOLD);

  // Draw selection highlight
  renderer.fillRect(0, 60 + selectedSettingIndex * 30 - 2, pageWidth - 1, 30);

  // Draw all settings
  for (int i = 0; i < settingsCount; i++) {
    const int settingY = 60 + i * 30;  // 30 pixels between settings
    const bool isSelected = (i == selectedSettingIndex);

    // Draw setting name
    renderer.drawText(UI_10_FONT_ID, 20, settingY, settingsList[i].name, !isSelected);

    // Draw value based on setting type
    std::string valueText;
    if (settingsList[i].type == SettingType::TOGGLE && settingsList[i].valuePtr != nullptr) {
      const bool value = SETTINGS.*(settingsList[i].valuePtr);
      valueText = value ? "ON" : "OFF";
    } else if (settingsList[i].type == SettingType::ENUM && settingsList[i].valuePtr != nullptr) {
      const uint8_t value = SETTINGS.*(settingsList[i].valuePtr);
      valueText = settingsList[i].enumValues[value];
    } else if (settingsList[i].type == SettingType::VALUE && settingsList[i].valuePtr != nullptr) {
      valueText = std::to_string(SETTINGS.*(settingsList[i].valuePtr));
    }
    if (!valueText.empty()) {
      const auto width = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      renderer.drawText(UI_10_FONT_ID, pageWidth - 20 - width, settingY, valueText.c_str(), !isSelected);
    }
  }

  renderer.drawText(SMALL_FONT_ID, pageWidth - 20 - renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION),
                    pageHeight - 60, CROSSPOINT_VERSION);

  const auto labels = mappedInput.mapLabels("« Back", "Toggle", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
