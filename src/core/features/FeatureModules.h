#pragma once

#include <Arduino.h>

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
  static bool isEnabled(const char* featureKey);
  static bool hasCapability(Capability capability);
  static bool supportsSettingAction(SettingAction action);

  static Activity* createSettingsSubActivity(SettingAction action, GfxRenderer& renderer,
                                             MappedInputManager& mappedInput, const std::function<void()>& onComplete,
                                             const std::function<void(bool)>& onCompleteBool);

  static void onFontFamilySettingChanged(uint8_t newValue);
  static void onWebSettingsApplied();
  static void onUploadCompleted(const String& uploadPath, const String& uploadFileName);

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
  /**
   * Scan/reload the user-font library and (if a USER_SD font is selected)
   * reload the active font family.  Returns metadata for building a JSON
   * response without the caller needing to know about UserFontManager.
   */
  static FontScanResult onFontScanRequested();
};

}  // namespace core
