#pragma once

#include <Arduino.h>
#include <FeatureFlags.h>
#include <Logging.h>

/**
 * FeatureManifest provides compile-time and runtime information about
 * which compile-time features are included in the firmware build.
 *
 * This enables:
 * - Runtime feature detection
 * - Debug logging of configuration
 * - External tools to query device capabilities
 * - Settings UI to hide unavailable features
 */
class FeatureManifest {
 public:
  // Compile-time feature detection (constexpr for zero runtime cost)
  static constexpr bool hasExtendedFonts() { return ENABLE_EXTENDED_FONTS != 0; }
  static constexpr bool hasBookerlyFonts() { return ENABLE_BOOKERLY_FONTS != 0; }
  static constexpr bool hasNotoSansFonts() { return ENABLE_NOTOSANS_FONTS != 0; }
  static constexpr bool hasOpenDyslexicFonts() { return ENABLE_OPENDYSLEXIC_FONTS != 0; }
  static constexpr bool hasImageSleep() { return ENABLE_IMAGE_SLEEP != 0; }
  static constexpr bool hasBookImages() { return ENABLE_BOOK_IMAGES != 0; }
  static constexpr bool hasMarkdown() { return ENABLE_MARKDOWN != 0; }
  static constexpr bool hasIntegrations() { return ENABLE_INTEGRATIONS != 0; }
  static constexpr bool hasKOReaderSync() { return ENABLE_KOREADER_SYNC != 0; }
  static constexpr bool hasCalibreSync() { return ENABLE_CALIBRE_SYNC != 0; }
  static constexpr bool hasBackgroundServer() { return ENABLE_BACKGROUND_SERVER != 0; }
  static constexpr bool hasHomeMediaPicker() { return ENABLE_HOME_MEDIA_PICKER != 0; }
  static constexpr bool hasWebPokedexPlugin() { return ENABLE_WEB_POKEDEX_PLUGIN != 0; }
  static constexpr bool hasEpubSupport() { return ENABLE_EPUB_SUPPORT != 0; }
  static constexpr bool hasHyphenation() { return ENABLE_HYPHENATION != 0; }
  static constexpr bool hasXtcSupport() { return ENABLE_XTC_SUPPORT != 0; }
  static constexpr bool hasLyraTheme() { return ENABLE_LYRA_THEME != 0; }
  static constexpr bool hasOtaUpdates() { return ENABLE_OTA_UPDATES != 0; }
  static constexpr bool hasTodoPlanner() { return ENABLE_TODO_PLANNER != 0; }
  static constexpr bool hasDarkMode() { return ENABLE_DARK_MODE != 0; }
  static constexpr bool hasVisualCoverPicker() { return ENABLE_VISUAL_COVER_PICKER != 0; }
  static constexpr bool hasBleWifiProvisioning() { return ENABLE_BLE_WIFI_PROVISIONING != 0; }
  static constexpr bool hasUserFonts() { return ENABLE_USER_FONTS != 0; }
  static constexpr bool hasWebWifiSetup() { return ENABLE_WEB_WIFI_SETUP != 0; }
  static constexpr bool hasUsbMassStorage() { return ENABLE_USB_MASS_STORAGE != 0; }

  /**
   * Count how many compile-time features are enabled.
   * Useful for debugging and capacity planning.
   */
  static constexpr int enabledFeatureCount() {
    return hasExtendedFonts() + hasBookerlyFonts() + hasNotoSansFonts() + hasOpenDyslexicFonts() + hasImageSleep() +
           hasBookImages() + hasMarkdown() + hasIntegrations() + hasKOReaderSync() + hasCalibreSync() +
           hasBackgroundServer() + hasHomeMediaPicker() + hasWebPokedexPlugin() + hasEpubSupport() + hasHyphenation() +
           hasXtcSupport() + hasLyraTheme() + hasOtaUpdates() + hasTodoPlanner() + hasDarkMode() +
           hasVisualCoverPicker() + hasBleWifiProvisioning() + hasUserFonts() + hasWebWifiSetup() + hasUsbMassStorage();
  }

  /**
   * Get a human-readable build configuration string.
   * Format: "extended_fonts,image_sleep,markdown" (enabled features only)
   */
  static String getBuildString() {
    String build = "";
    bool first = true;

    auto addFeature = [&](bool enabled, const char* name) {
      if (enabled) {
        if (!first) build += ",";
        build += name;
        first = false;
      }
    };

    addFeature(hasExtendedFonts(), "extended_fonts");
    addFeature(hasBookerlyFonts(), "bookerly_fonts");
    addFeature(hasNotoSansFonts(), "notosans_fonts");
    addFeature(hasOpenDyslexicFonts(), "opendyslexic_fonts");
    addFeature(hasImageSleep(), "image_sleep");
    addFeature(hasBookImages(), "book_images");
    addFeature(hasMarkdown(), "markdown");
    addFeature(hasIntegrations(), "integrations");
    addFeature(hasKOReaderSync(), "koreader_sync");
    addFeature(hasCalibreSync(), "calibre_sync");
    addFeature(hasBackgroundServer(), "background_server");
    addFeature(hasHomeMediaPicker(), "home_media_picker");
    addFeature(hasWebPokedexPlugin(), "web_pokedex_plugin");
    addFeature(hasEpubSupport(), "epub_support");
    addFeature(hasHyphenation(), "hyphenation");
    addFeature(hasXtcSupport(), "xtc_support");
    addFeature(hasLyraTheme(), "lyra_theme");
    addFeature(hasOtaUpdates(), "ota_updates");
    addFeature(hasTodoPlanner(), "todo_planner");
    addFeature(hasDarkMode(), "dark_mode");
    addFeature(hasVisualCoverPicker(), "visual_cover_picker");
    addFeature(hasBleWifiProvisioning(), "ble_wifi_provisioning");
    addFeature(hasUserFonts(), "user_fonts");
    addFeature(hasWebWifiSetup(), "web_wifi_setup");
    addFeature(hasUsbMassStorage(), "usb_mass_storage");

    return build.isEmpty() ? "lean" : build;
  }

  /**
   * JSON serialization for feature capability API.
   * Returns: {"extended_fonts":true,"image_sleep":false,...}
   */
  static String toJson() {
    String json = "{";
    json += "\"extended_fonts\":" + String(hasExtendedFonts() ? "true" : "false");
    json += ",\"bookerly_fonts\":" + String(hasBookerlyFonts() ? "true" : "false");
    json += ",\"notosans_fonts\":" + String(hasNotoSansFonts() ? "true" : "false");
    json += ",\"opendyslexic_fonts\":" + String(hasOpenDyslexicFonts() ? "true" : "false");
    json += ",\"image_sleep\":" + String(hasImageSleep() ? "true" : "false");
    json += ",\"book_images\":" + String(hasBookImages() ? "true" : "false");
    json += ",\"markdown\":" + String(hasMarkdown() ? "true" : "false");
    json += ",\"integrations\":" + String(hasIntegrations() ? "true" : "false");
    json += ",\"koreader_sync\":" + String(hasKOReaderSync() ? "true" : "false");
    json += ",\"calibre_sync\":" + String(hasCalibreSync() ? "true" : "false");
    json += ",\"background_server\":" + String(hasBackgroundServer() ? "true" : "false");
    json += ",\"home_media_picker\":" + String(hasHomeMediaPicker() ? "true" : "false");
    json += ",\"web_pokedex_plugin\":" + String(hasWebPokedexPlugin() ? "true" : "false");
    json += ",\"epub_support\":" + String(hasEpubSupport() ? "true" : "false");
    json += ",\"hyphenation\":" + String(hasHyphenation() ? "true" : "false");
    json += ",\"xtc_support\":" + String(hasXtcSupport() ? "true" : "false");
    json += ",\"lyra_theme\":" + String(hasLyraTheme() ? "true" : "false");
    json += ",\"ota_updates\":" + String(hasOtaUpdates() ? "true" : "false");
    json += ",\"todo_planner\":" + String(hasTodoPlanner() ? "true" : "false");
    json += ",\"dark_mode\":" + String(hasDarkMode() ? "true" : "false");
    json += ",\"visual_cover_picker\":" + String(hasVisualCoverPicker() ? "true" : "false");
    json += ",\"ble_wifi_provisioning\":" + String(hasBleWifiProvisioning() ? "true" : "false");
    json += ",\"user_fonts\":" + String(hasUserFonts() ? "true" : "false");
    json += ",\"web_wifi_setup\":" + String(hasWebWifiSetup() ? "true" : "false");
    json += ",\"usb_mass_storage\":" + String(hasUsbMassStorage() ? "true" : "false");
    json += "}";
    return json;
  }

  /**
   * Print compile-time feature configuration to Serial for debugging.
   * Call this during setup() to log the build configuration.
   */
  static void printToSerial() {
    LOG_INF("FEATURES", "CrossPoint Reader build configuration:");
    LOG_INF("FEATURES", "  Extended Fonts:    %s", hasExtendedFonts() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Bookerly Fonts:    %s", hasBookerlyFonts() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  NotoSans Fonts:    %s", hasNotoSansFonts() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  OpenDyslexic:      %s", hasOpenDyslexicFonts() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Image Sleep:       %s", hasImageSleep() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Book Images:       %s", hasBookImages() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Markdown:          %s", hasMarkdown() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Integrations:      %s", hasIntegrations() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  KOReader Sync:     %s", hasKOReaderSync() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Calibre Sync:      %s", hasCalibreSync() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Background Server: %s", hasBackgroundServer() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Home Media Picker: %s", hasHomeMediaPicker() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Web Pokedex:       %s", hasWebPokedexPlugin() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  EPUB Support:      %s", hasEpubSupport() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Hyphenation:       %s", hasHyphenation() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  XTC Support:       %s", hasXtcSupport() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Lyra Theme:        %s", hasLyraTheme() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  OTA Updates:       %s", hasOtaUpdates() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Todo Planner:      %s", hasTodoPlanner() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Dark Mode:         %s", hasDarkMode() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Visual Cov Picker: %s", hasVisualCoverPicker() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  BLE WiFi Provision: %s", hasBleWifiProvisioning() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  User Fonts:        %s", hasUserFonts() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  Web WiFi Setup:    %s", hasWebWifiSetup() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "  USB Mass Storage:  %s", hasUsbMassStorage() ? "ENABLED " : "DISABLED");
    LOG_INF("FEATURES", "%d/25 compile-time features enabled", enabledFeatureCount());
    LOG_INF("FEATURES", "Build: %s", getBuildString().c_str());
  }
};
