#include "core/features/FeatureModules.h"

#include <FeatureFlags.h>
#include <Logging.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

#if ENABLE_EPUB_SUPPORT
#include <Epub.h>
#endif

#include "CrossPointSettings.h"
#if ENABLE_EPUB_SUPPORT
#include "SpiBusMutex.h"
#endif
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/ButtonRemapActivity.h"
#include "activities/settings/ClearCacheActivity.h"
#include "activities/settings/FactoryResetActivity.h"
#include "activities/settings/LanguageSelectActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "core/features/FeatureCatalog.h"
#if ENABLE_EPUB_SUPPORT
#include "util/StringUtils.h"
#endif

#if ENABLE_WEB_POKEDEX_PLUGIN
#include "html/PokedexPluginPageHtml.generated.h"
#endif

#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/settings/CalibreSettingsActivity.h"
#endif

#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
#include "activities/settings/KOReaderSettingsActivity.h"
#include "lib/KOReaderSync/KOReaderCredentialStore.h"
#endif

#if ENABLE_OTA_UPDATES
#include "activities/settings/OtaUpdateActivity.h"
#include "network/OtaUpdater.h"
#endif

#if ENABLE_USER_FONTS
#include "UserFontManager.h"
#endif

#if ENABLE_TODO_PLANNER
#include "activities/todo/TodoActivity.h"
#include "activities/todo/TodoFallbackActivity.h"
#endif

namespace core {

#if ENABLE_WEB_POKEDEX_PLUGIN
static_assert(PokedexPluginPageHtmlCompressedSize == sizeof(PokedexPluginPageHtml),
              "Pokedex page compressed size mismatch");
#endif

namespace {
#if ENABLE_OTA_UPDATES
enum class OtaWebCheckState { Idle, Checking, Done };

struct OtaWebCheckData {
  std::atomic<OtaWebCheckState> state{OtaWebCheckState::Idle};
  bool available = false;
  std::string latestVersion;
  std::string message;
  int errorCode = 0;
};

OtaWebCheckData otaWebCheckData;

void otaWebCheckTask(void* param) {
  auto* updater = static_cast<OtaUpdater*>(param);
  const auto result = updater->checkForUpdate();

  otaWebCheckData.errorCode = static_cast<int>(result);
  if (result == OtaUpdater::OK) {
    otaWebCheckData.available = updater->isUpdateNewer();
    otaWebCheckData.latestVersion = updater->getLatestVersion();
    otaWebCheckData.message =
        otaWebCheckData.available ? "Update available. Install from device Settings." : "Already on latest version.";
  } else {
    otaWebCheckData.available = false;
    const String& error = updater->getLastError();
    otaWebCheckData.message = error.length() > 0 ? error.c_str() : "Update check failed";
  }

  otaWebCheckData.state.store(OtaWebCheckState::Done, std::memory_order_release);
  delete updater;
  vTaskDelete(nullptr);
}
#endif
}  // namespace

bool FeatureModules::isEnabled(const char* featureKey) { return FeatureCatalog::isEnabled(featureKey); }

bool FeatureModules::hasCapability(const Capability capability) {
  switch (capability) {
    case Capability::BackgroundServer:
      return isEnabled("background_server");
    case Capability::CalibreSync:
      return isEnabled("calibre_sync");
    case Capability::DarkMode:
      return isEnabled("dark_mode");
    case Capability::EpubSupport:
      return isEnabled("epub_support");
    case Capability::KoreaderSync:
      return isEnabled("koreader_sync");
    case Capability::LyraTheme:
      return isEnabled("lyra_theme");
    case Capability::MarkdownSupport:
      return isEnabled("markdown");
    case Capability::OtaUpdates:
      return isEnabled("ota_updates");
    case Capability::TodoPlanner:
      return isEnabled("todo_planner");
    case Capability::UsbMassStorage:
      return isEnabled("usb_mass_storage");
    case Capability::UserFonts:
      return isEnabled("user_fonts");
    case Capability::WebPokedexPlugin:
      return isEnabled("web_pokedex_plugin");
    case Capability::WebWifiSetup:
      return isEnabled("web_wifi_setup");
    case Capability::XtcSupport:
      return isEnabled("xtc_support");
  }
  return false;
}

bool FeatureModules::supportsSettingAction(const SettingAction action) {
  switch (action) {
    case SettingAction::RemapFrontButtons:
    case SettingAction::Network:
    case SettingAction::ClearCache:
    case SettingAction::FactoryReset:
      return true;
    case SettingAction::KOReaderSync:
      return hasCapability(Capability::KoreaderSync);
    case SettingAction::OPDSBrowser:
      return hasCapability(Capability::CalibreSync);
    case SettingAction::CheckForUpdates:
    case SettingAction::Language:
      return hasCapability(Capability::OtaUpdates);
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

Activity* FeatureModules::createOpdsBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                    const std::function<void()>& onBack) {
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  if (!hasCapability(Capability::CalibreSync)) {
    return nullptr;
  }
  return new OpdsBookBrowserActivity(renderer, mappedInput, onBack);
#else
  (void)renderer;
  (void)mappedInput;
  (void)onBack;
  return nullptr;
#endif
}

Activity* FeatureModules::createTodoPlannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                    std::string filePath, std::string dateTitle,
                                                    const std::function<void()>& onBack) {
#if ENABLE_TODO_PLANNER
  if (!hasCapability(Capability::TodoPlanner)) {
    return nullptr;
  }
  return new TodoActivity(renderer, mappedInput, std::move(filePath), std::move(dateTitle), onBack);
#else
  (void)renderer;
  (void)mappedInput;
  (void)filePath;
  (void)dateTitle;
  (void)onBack;
  return nullptr;
#endif
}

Activity* FeatureModules::createTodoFallbackActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::string dateText, const std::function<void()>& onBack) {
#if ENABLE_TODO_PLANNER
  if (!hasCapability(Capability::TodoPlanner)) {
    return nullptr;
  }
  return new TodoFallbackActivity(renderer, mappedInput, std::move(dateText), onBack);
#else
  (void)renderer;
  (void)mappedInput;
  (void)dateText;
  (void)onBack;
  return nullptr;
#endif
}

std::string FeatureModules::getKoreaderUsername() {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  return KOREADER_STORE.getUsername();
#else
  return "";
#endif
}

std::string FeatureModules::getKoreaderPassword() {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  return KOREADER_STORE.getPassword();
#else
  return "";
#endif
}

std::string FeatureModules::getKoreaderServerUrl() {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  return KOREADER_STORE.getServerUrl();
#else
  return "";
#endif
}

uint8_t FeatureModules::getKoreaderMatchMethod() {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod());
#else
  return 0;
#endif
}

void FeatureModules::setKoreaderUsername(const std::string& username) {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  KOREADER_STORE.setCredentials(username, KOREADER_STORE.getPassword());
  KOREADER_STORE.saveToFile();
#else
  (void)username;
#endif
}

void FeatureModules::setKoreaderPassword(const std::string& password) {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), password);
  KOREADER_STORE.saveToFile();
#else
  (void)password;
#endif
}

void FeatureModules::setKoreaderServerUrl(const std::string& serverUrl) {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  KOREADER_STORE.setServerUrl(serverUrl);
  KOREADER_STORE.saveToFile();
#else
  (void)serverUrl;
#endif
}

void FeatureModules::setKoreaderMatchMethod(const uint8_t method) {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  const auto selectedMethod = method == static_cast<uint8_t>(DocumentMatchMethod::BINARY)
                                  ? DocumentMatchMethod::BINARY
                                  : DocumentMatchMethod::FILENAME;
  KOREADER_STORE.setMatchMethod(selectedMethod);
  KOREADER_STORE.saveToFile();
#else
  (void)method;
#endif
}

std::vector<std::string> FeatureModules::getUserFontFamilies() {
#if ENABLE_USER_FONTS
  auto& manager = UserFontManager::getInstance();
  manager.scanFonts();
  return manager.getAvailableFonts();
#else
  return {};
#endif
}

uint8_t FeatureModules::getSelectedUserFontFamilyIndex() {
#if ENABLE_USER_FONTS
  auto& manager = UserFontManager::getInstance();
  manager.scanFonts();
  const auto& fonts = manager.getAvailableFonts();
  if (fonts.empty()) {
    return 0;
  }

  const std::string selectedFont = SETTINGS.userFontPath;
  const auto it = std::find(fonts.begin(), fonts.end(), selectedFont);
  if (it == fonts.end()) {
    return 0;
  }
  return static_cast<uint8_t>(std::distance(fonts.begin(), it));
#else
  return 0;
#endif
}

void FeatureModules::setSelectedUserFontFamilyIndex(const uint8_t index) {
#if ENABLE_USER_FONTS
  auto& manager = UserFontManager::getInstance();
  manager.scanFonts();
  const auto& fonts = manager.getAvailableFonts();
  if (fonts.empty()) {
    SETTINGS.userFontPath[0] = '\0';
    if (SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
      SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
      manager.unloadCurrentFont();
    }
    return;
  }

  const size_t selectedIndex = std::min(static_cast<size_t>(index), fonts.size() - 1);
  strncpy(SETTINGS.userFontPath, fonts[selectedIndex].c_str(), sizeof(SETTINGS.userFontPath) - 1);
  SETTINGS.userFontPath[sizeof(SETTINGS.userFontPath) - 1] = '\0';
  if (SETTINGS.fontFamily == CrossPointSettings::USER_SD && !manager.loadFontFamily(SETTINGS.userFontPath)) {
    SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
  }
#else
  (void)index;
#endif
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

void FeatureModules::onWebFileChanged(const String& filePath) {
#if ENABLE_EPUB_SUPPORT
  if (StringUtils::checkFileExtension(filePath, ".epub")) {
    Epub(filePath.c_str(), "/.crosspoint").clearCache();
    LOG_DBG("FEATURES", "Cleared epub cache for: %s", filePath.c_str());
  }
#else
  (void)filePath;
#endif
}

bool FeatureModules::tryGetDocumentCoverPath(const String& documentPath, std::string& outCoverPath) {
#if ENABLE_EPUB_SUPPORT
  String lowerPath = documentPath;
  lowerPath.toLowerCase();
  if (!lowerPath.endsWith(".epub")) {
    return false;
  }

  Epub epub(documentPath.c_str(), "/.crosspoint");
  SpiBusMutex::Guard guard;
  if (!epub.load(false)) {
    return false;
  }

  outCoverPath = epub.getThumbBmpPath();
  return !outCoverPath.empty();
#else
  (void)documentPath;
  outCoverPath.clear();
  return false;
#endif
}

FeatureModules::WebCompressedPayload FeatureModules::getPokedexPluginPagePayload() {
#if ENABLE_WEB_POKEDEX_PLUGIN
  return {true, PokedexPluginPageHtml, PokedexPluginPageHtmlCompressedSize};
#else
  return {false, nullptr, 0};
#endif
}

FeatureModules::OtaWebStartResult FeatureModules::startOtaWebCheck() {
#if ENABLE_OTA_UPDATES
  if (otaWebCheckData.state.load(std::memory_order_acquire) == OtaWebCheckState::Checking) {
    return OtaWebStartResult::AlreadyChecking;
  }

  otaWebCheckData.available = false;
  otaWebCheckData.latestVersion.clear();
  otaWebCheckData.message = "Checking...";
  otaWebCheckData.errorCode = 0;
  otaWebCheckData.state.store(OtaWebCheckState::Checking, std::memory_order_release);

  auto* updater = new OtaUpdater();
  if (xTaskCreate(otaWebCheckTask, "OtaWebCheckTask", 4096, updater, 1, nullptr) != pdPASS) {
    delete updater;
    otaWebCheckData.state.store(OtaWebCheckState::Idle, std::memory_order_release);
    return OtaWebStartResult::StartTaskFailed;
  }

  return OtaWebStartResult::Started;
#else
  return OtaWebStartResult::Disabled;
#endif
}

FeatureModules::OtaWebCheckSnapshot FeatureModules::getOtaWebCheckSnapshot() {
#if ENABLE_OTA_UPDATES
  OtaWebCheckSnapshot snapshot;
  const OtaWebCheckState state = otaWebCheckData.state.load(std::memory_order_acquire);
  snapshot.status = state == OtaWebCheckState::Checking
                        ? OtaWebCheckStatus::Checking
                        : (state == OtaWebCheckState::Done ? OtaWebCheckStatus::Done : OtaWebCheckStatus::Idle);
  snapshot.available = otaWebCheckData.available;
  snapshot.latestVersion = otaWebCheckData.latestVersion;
  snapshot.message = otaWebCheckData.message;
  snapshot.errorCode = otaWebCheckData.errorCode;
  return snapshot;
#else
  return {};
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

bool FeatureModules::shouldExposeHomeAction(const HomeOptionalAction action, const bool hasOpdsUrl) {
  switch (action) {
    case HomeOptionalAction::OpdsBrowser:
      return hasCapability(Capability::CalibreSync) && hasOpdsUrl;
    case HomeOptionalAction::TodoPlanner:
      return hasCapability(Capability::TodoPlanner);
  }
  return false;
}

bool FeatureModules::shouldRegisterWebRoute(const WebOptionalRoute route) {
  switch (route) {
    case WebOptionalRoute::PokedexPluginPage:
      return hasCapability(Capability::WebPokedexPlugin);
    case WebOptionalRoute::UserFontsApi:
      return hasCapability(Capability::UserFonts);
    case WebOptionalRoute::WebWifiSetupApi:
      return hasCapability(Capability::WebWifiSetup);
    case WebOptionalRoute::OtaApi:
      return hasCapability(Capability::OtaUpdates);
  }
  return false;
}

}  // namespace core
