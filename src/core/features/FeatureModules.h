#pragma once

#include <Arduino.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Activity;
class CrossPointWebServer;
class GfxRenderer;
class MappedInputManager;
class WebServer;
enum class SettingAction;

namespace core {

enum class WebOptionalRoute {
  PokedexPluginPage,
  UserFontsApi,
  WebWifiSetupApi,
  OtaApi,
};

enum class HomeOptionalAction {
  OpdsBrowser,
  TodoPlanner,
};

enum class Capability {
  BackgroundServer,
  CalibreSync,
  DarkMode,
  EpubSupport,
  KoreaderSync,
  LyraTheme,
  MarkdownSupport,
  OtaUpdates,
  TodoPlanner,
  UsbMassStorage,
  UserFonts,
  WebPokedexPlugin,
  WebWifiSetup,
  XtcSupport,
};

class FeatureModules {
 public:
  enum class ReaderOpenStatus {
    Opened,
    Unsupported,
    LoadFailed,
  };

  struct ReaderOpenResult {
    ReaderOpenStatus status = ReaderOpenStatus::LoadFailed;
    Activity* activity = nullptr;
    const char* logMessage = nullptr;
    const char* uiMessage = nullptr;
  };

  struct HomeCardDataResult {
    bool handled = false;
    bool loaded = false;
    std::string title;
    std::string author;
    std::string coverPath;
  };

  struct RecentBookDataResult {
    bool handled = false;
    std::string title;
    std::string author;
    std::string coverPath;
  };

  static bool isEnabled(const char* featureKey);
  static bool hasCapability(Capability capability);
  static String getBuildString();
  static String getFeatureMapJson();
  static bool supportsSettingAction(SettingAction action);
  static ReaderOpenResult createReaderActivityForPath(
      const std::string& path, GfxRenderer& renderer, MappedInputManager& mappedInput,
      const std::function<void(const std::string&)>& onBackToLibraryPath, const std::function<void()>& onBackHome);
  static HomeCardDataResult resolveHomeCardData(const std::string& path, int thumbHeight);
  static RecentBookDataResult resolveRecentBookData(const std::string& path);

  static Activity* createSettingsSubActivity(SettingAction action, GfxRenderer& renderer,
                                             MappedInputManager& mappedInput, const std::function<void()>& onComplete,
                                             const std::function<void(bool)>& onCompleteBool);
  static Activity* createOpdsBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::function<void()>& onBack);
  static Activity* createTodoPlannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::string filePath, std::string dateTitle,
                                             const std::function<void()>& onBack);
  static Activity* createTodoFallbackActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              std::string dateText, const std::function<void()>& onBack);

  static void onFontFamilySettingChanged(uint8_t newValue);
  static void onWebSettingsApplied();
  static void onUploadCompleted(const String& uploadPath, const String& uploadFileName);
  static void onWebFileChanged(const String& filePath);
  static bool tryGetDocumentCoverPath(const String& documentPath, std::string& outCoverPath);

  static bool shouldRegisterWebRoute(WebOptionalRoute route);
  static bool shouldExposeHomeAction(HomeOptionalAction action, bool hasOpdsUrl);

  static std::string getKoreaderUsername();
  static std::string getKoreaderPassword();
  static std::string getKoreaderServerUrl();
  static uint8_t getKoreaderMatchMethod();
  static void setKoreaderUsername(const std::string& username);
  static void setKoreaderPassword(const std::string& password);
  static void setKoreaderServerUrl(const std::string& serverUrl);
  static void setKoreaderMatchMethod(uint8_t method);
  static std::vector<std::string> getUserFontFamilies();
  static uint8_t getSelectedUserFontFamilyIndex();
  static void setSelectedUserFontFamilyIndex(uint8_t index);

  struct FontScanResult {
    bool available;  // false when ENABLE_USER_FONTS is off
    int familyCount;
    bool activeLoaded;
  };

  struct WebCompressedPayload {
    bool available;
    const char* data;
    size_t compressedSize;
  };

  enum class OtaWebStartResult {
    Disabled,
    Started,
    AlreadyChecking,
    StartTaskFailed,
  };

  enum class OtaWebCheckStatus {
    Disabled,
    Idle,
    Checking,
    Done,
  };

  struct OtaWebCheckSnapshot {
    OtaWebCheckStatus status = OtaWebCheckStatus::Disabled;
    bool available = false;
    std::string latestVersion;
    std::string message;
    int errorCode = 0;
  };

  static WebCompressedPayload getPokedexPluginPagePayload();
  static OtaWebStartResult startOtaWebCheck();
  static OtaWebCheckSnapshot getOtaWebCheckSnapshot();

  /**
   * Scan/reload the user-font library and (if a USER_SD font is selected)
   * reload the active font family.  Returns metadata for building a JSON
   * response without the caller needing to know about UserFontManager.
   */
  static FontScanResult onFontScanRequested();
};

}  // namespace core
