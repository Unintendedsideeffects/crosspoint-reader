#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "FactoryResetActivity.h"
#include "FeatureFlags.h"
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
#include "CalibreSettingsActivity.h"
#endif
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
#include "KOReaderSettingsActivity.h"
#endif
#include "MappedInputManager.h"
#if ENABLE_OTA_UPDATES
#include "OtaUpdateActivity.h"
#endif
#include "SettingsList.h"
#include "activities/TaskShutdown.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

const char* SettingsActivity::categoryNames[categoryCount] = {"Display", "Reader", "Controls", "System"};

void SettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SettingsActivity*>(param);
  self->displayTaskLoop();
}

void SettingsActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  exitTaskRequested.store(false);
  taskHasExited.store(false);

  // Build per-category vectors from the shared settings list
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  for (auto& setting : getSettingsList()) {
    if (!setting.category) continue;
    if (strcmp(setting.category, "Display") == 0) {
      displaySettings.push_back(std::move(setting));
    } else if (strcmp(setting.category, "Reader") == 0) {
      readerSettings.push_back(std::move(setting));
    } else if (strcmp(setting.category, "Controls") == 0) {
      controlsSettings.push_back(std::move(setting));
    } else if (strcmp(setting.category, "System") == 0) {
      systemSettings.push_back(std::move(setting));
    }
    // Web-only categories (KOReader Sync, OPDS Browser) are skipped for device UI
  }

  // Append device-only ACTION items
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action("Remap Front Buttons", SettingAction::RemapFrontButtons));
  systemSettings.push_back(SettingInfo::Action("Network", SettingAction::Network));
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  systemSettings.push_back(SettingInfo::Action("KOReader Sync", SettingAction::KOReaderSync));
#endif
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  systemSettings.push_back(SettingInfo::Action("OPDS Browser", SettingAction::OPDSBrowser));
#endif
  systemSettings.push_back(SettingInfo::Action("Clear Cache", SettingAction::ClearCache));
  systemSettings.push_back(SettingInfo::Action("Factory Reset", SettingAction::FactoryReset));
#if ENABLE_OTA_UPDATES
  systemSettings.push_back(SettingInfo::Action("Check for updates", SettingAction::CheckForUpdates));
#endif

  // Reset selection to first category
  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;

  // Initialize with first category (Display)
  currentSettings = &displaySettings;
  settingsCount = static_cast<int>(displaySettings.size());

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&SettingsActivity::taskTrampoline, "SettingsActivityTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  TaskShutdown::requestExit(exitTaskRequested, taskHasExited, displayTaskHandle);
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  renderer.setDarkMode(SETTINGS.darkMode);
  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }
  bool hasChangedCategory = false;

  // Handle category selection on release (supports dual-side power tap Confirm)
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    enterCategory(selectedCategoryIndex);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
    return;
  }

  // Handle navigation
  buttonNavigator.onNextRelease([this] {
    selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1);
    updateRequired = true;
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1);
    updateRequired = true;
  });

  buttonNavigator.onNextContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount);
    updateRequired = true;
  });

  buttonNavigator.onPreviousContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
    updateRequired = true;
  });

  if (hasChangedCategory) {
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : 1;
    switch (selectedCategoryIndex) {
      case 0:
        currentSettings = &displaySettings;
        break;
      case 1:
        currentSettings = &readerSettings;
        break;
      case 2:
        currentSettings = &controlsSettings;
        break;
      case 3:
        currentSettings = &systemSettings;
        break;
    }
    settingsCount = static_cast<int>(currentSettings->size());
  }
}

void SettingsActivity::enterCategory(int categoryIndex) {
  if (selectedSettingIndex > 0) {
    toggleCurrentSetting();
    updateRequired = true;
    return;
  }

  if (categoryIndex < 0 || categoryIndex >= categoryCount) {
    return;
  }

  selectedCategoryIndex = categoryIndex;
  selectedSettingIndex = 1;

  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
    default:
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());

  updateRequired = true;
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    if (setting.enumValues.empty()) {
      return;
    }
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    const uint8_t maxIndex = static_cast<uint8_t>(setting.enumValues.size() - 1);
    const uint8_t normalizedValue = (currentValue > maxIndex) ? 0 : currentValue;
    SETTINGS.*(setting.valuePtr) = (normalizedValue + 1) % static_cast<uint8_t>(setting.enumValues.size());

    if (setting.valuePtr == &CrossPointSettings::frontButtonLayout) {
      SETTINGS.applyFrontButtonLayoutPreset(
          static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout));
    }
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto enterSubActivity = [this](Activity* activity) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(activity);
      xSemaphoreGive(renderingMutex);
    };

    auto onComplete = [this] {
      exitActivity();
      updateRequired = true;
    };

    auto onCompleteBool = [this](bool) {
      exitActivity();
      updateRequired = true;
    };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        enterSubActivity(new ButtonRemapActivity(renderer, mappedInput, onComplete));
        break;
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
      case SettingAction::KOReaderSync:
        enterSubActivity(new KOReaderSettingsActivity(renderer, mappedInput, onComplete));
        break;
#endif
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
      case SettingAction::OPDSBrowser:
        enterSubActivity(new CalibreSettingsActivity(renderer, mappedInput, onComplete));
        break;
#endif
      case SettingAction::Network:
        enterSubActivity(new WifiSelectionActivity(renderer, mappedInput, onCompleteBool, false));
        break;
      case SettingAction::ClearCache:
        enterSubActivity(new ClearCacheActivity(renderer, mappedInput, onComplete));
        break;
      case SettingAction::FactoryReset:
        enterSubActivity(new FactoryResetActivity(renderer, mappedInput, onComplete));
        break;
#if ENABLE_OTA_UPDATES
      case SettingAction::CheckForUpdates:
        enterSubActivity(new OtaUpdateActivity(renderer, mappedInput, onComplete));
        break;
#endif
      case SettingAction::None:
        // Do nothing
        break;
    }
  } else {
    return;
  }

  SETTINGS.enforceButtonLayoutConstraints();
  renderer.setDarkMode(SETTINGS.darkMode);
  SETTINGS.saveToFile();
}

void SettingsActivity::displayTaskLoop() {
  while (!exitTaskRequested.load()) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      if (!exitTaskRequested.load()) {
        render();
      }
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  taskHasExited.store(true);
  vTaskDelete(nullptr);
}

void SettingsActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Settings");

  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    tabs.push_back({categoryNames[i], selectedCategoryIndex == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  const auto& settings = *currentSettings;
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      settingsCount, selectedSettingIndex - 1, [&settings](int index) { return std::string(settings[index].name); },
      nullptr, nullptr,
      [&settings](int i) {
        std::string valueText = "";
        if (settings[i].type == SettingType::TOGGLE && settings[i].valuePtr != nullptr) {
          const bool value = SETTINGS.*(settings[i].valuePtr);
          valueText = value ? "ON" : "OFF";
        } else if (settings[i].type == SettingType::ENUM && settings[i].valuePtr != nullptr) {
          const uint8_t value = SETTINGS.*(settings[i].valuePtr);
          if (value < settings[i].enumValues.size()) {
            valueText = settings[i].enumValues[value];
          } else {
            valueText = std::to_string(value);
          }
        } else if (settings[i].type == SettingType::VALUE && settings[i].valuePtr != nullptr) {
          valueText = std::to_string(SETTINGS.*(settings[i].valuePtr));
        }
        return valueText;
      });

  // Draw version text
  renderer.drawText(SMALL_FONT_ID,
                    pageWidth - metrics.versionTextRightX - renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION),
                    metrics.versionTextY, CROSSPOINT_VERSION);

  // Draw help text
  const auto labels = mappedInput.mapLabels("« Back", "Toggle", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
