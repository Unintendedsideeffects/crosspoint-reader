#include "core/features/FeatureModules.h"

#include <FeatureFlags.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/ButtonRemapActivity.h"
#include "activities/settings/ClearCacheActivity.h"
#include "activities/settings/FactoryResetActivity.h"
#include "activities/settings/LanguageSelectActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "core/features/FeatureCatalog.h"

#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
#include "activities/settings/CalibreSettingsActivity.h"
#endif

#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
#include "activities/settings/KOReaderSettingsActivity.h"
#endif

#if ENABLE_OTA_UPDATES
#include "activities/settings/OtaUpdateActivity.h"
#endif

#if ENABLE_USER_FONTS
#include "UserFontManager.h"
#endif

namespace core {

bool FeatureModules::isEnabled(const char* featureKey) { return FeatureCatalog::isEnabled(featureKey); }

bool FeatureModules::supportsSettingAction(const SettingAction action) {
  switch (action) {
    case SettingAction::RemapFrontButtons:
    case SettingAction::Network:
    case SettingAction::ClearCache:
    case SettingAction::FactoryReset:
      return true;
    case SettingAction::KOReaderSync:
      return isEnabled("koreader_sync");
    case SettingAction::OPDSBrowser:
      return isEnabled("calibre_sync");
    case SettingAction::CheckForUpdates:
    case SettingAction::Language:
      return isEnabled("ota_updates");
    case SettingAction::None:
      return false;
  }
  return false;
}

Activity* FeatureModules::createSettingsSubActivity(const SettingAction action, GfxRenderer& renderer,
                                                    MappedInputManager& mappedInput,
                                                    const std::function<void()>& onComplete,
                                                    const std::function<void(bool)>& onCompleteBool) {
  if (!supportsSettingAction(action)) {
    return nullptr;
  }

  switch (action) {
    case SettingAction::RemapFrontButtons:
      return new ButtonRemapActivity(renderer, mappedInput, onComplete);
    case SettingAction::Network:
      return new WifiSelectionActivity(renderer, mappedInput, onCompleteBool, false);
    case SettingAction::ClearCache:
      return new ClearCacheActivity(renderer, mappedInput, onComplete);
    case SettingAction::FactoryReset:
      return new FactoryResetActivity(renderer, mappedInput, onComplete);
    case SettingAction::KOReaderSync:
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
      return new KOReaderSettingsActivity(renderer, mappedInput, onComplete);
#else
      return nullptr;
#endif
    case SettingAction::OPDSBrowser:
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
      return new CalibreSettingsActivity(renderer, mappedInput, onComplete);
#else
      return nullptr;
#endif
    case SettingAction::CheckForUpdates:
#if ENABLE_OTA_UPDATES
      return new OtaUpdateActivity(renderer, mappedInput, onComplete);
#else
      return nullptr;
#endif
    case SettingAction::Language:
#if ENABLE_OTA_UPDATES
      return new LanguageSelectActivity(renderer, mappedInput, onComplete);
#else
      return nullptr;
#endif
    case SettingAction::None:
      return nullptr;
  }

  return nullptr;
}

void FeatureModules::onFontFamilySettingChanged(const uint8_t newValue) {
#if ENABLE_USER_FONTS
  auto& fontManager = UserFontManager::getInstance();
  if (newValue == CrossPointSettings::USER_SD) {
    if (!fontManager.loadFontFamily(SETTINGS.userFontPath)) {
      SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
    }
  } else {
    fontManager.unloadCurrentFont();
  }
#else
  (void)newValue;
#endif
}

void FeatureModules::onWebSettingsApplied() {
#if ENABLE_USER_FONTS
  if (SETTINGS.fontFamily == CrossPointSettings::USER_SD &&
      !UserFontManager::getInstance().loadFontFamily(SETTINGS.userFontPath)) {
    SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
  }
#endif
}

void FeatureModules::onUploadCompleted(const String& uploadPath, const String& uploadFileName) {
#if ENABLE_USER_FONTS
  String normalizedUploadFileName = uploadFileName;
  normalizedUploadFileName.toLowerCase();
  if (uploadPath == "/fonts" && normalizedUploadFileName.endsWith(".cpf")) {
    UserFontManager::getInstance().scanFonts();
  }
#else
  (void)uploadPath;
  (void)uploadFileName;
#endif
}

FeatureModules::FontScanResult FeatureModules::onFontScanRequested() {
#if ENABLE_USER_FONTS
  auto& fontManager = UserFontManager::getInstance();
  fontManager.scanFonts();

  bool activeLoaded = true;
  if (SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
    activeLoaded = fontManager.loadFontFamily(SETTINGS.userFontPath);
    if (!activeLoaded) {
      SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
      if (!SETTINGS.saveToFile()) {
        LOG_WRN("FEATURES", "Failed to persist font fallback after rescan");
      }
    }
  }
  return {true, static_cast<int>(fontManager.getAvailableFonts().size()), activeLoaded};
#else
  return {false, 0, false};
#endif
}

bool FeatureModules::shouldRegisterWebRoute(const WebOptionalRoute route) {
  switch (route) {
    case WebOptionalRoute::PokedexPluginPage:
      return isEnabled("web_pokedex_plugin");
    case WebOptionalRoute::UserFontsApi:
      return isEnabled("user_fonts");
    case WebOptionalRoute::WebWifiSetupApi:
      return isEnabled("web_wifi_setup");
    case WebOptionalRoute::OtaApi:
      return isEnabled("ota_updates");
  }
  return false;
}

}  // namespace core
