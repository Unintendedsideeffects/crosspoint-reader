#pragma once

#include <Arduino.h>
#include <FeatureFlags.h>

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
  static constexpr bool hasOpenDyslexicFonts() { return ENABLE_OPENDYSLEXIC_FONTS != 0; }
  static constexpr bool hasImageSleep() { return ENABLE_IMAGE_SLEEP != 0; }
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
  static constexpr bool hasBleWifiProvisioning() { return ENABLE_BLE_WIFI_PROVISIONING != 0; }

  /**
   * Count how many compile-time features are enabled.
   * Useful for debugging and capacity planning.
   */
  static constexpr int enabledFeatureCount() {
    return hasExtendedFonts() + hasOpenDyslexicFonts() + hasImageSleep() + hasMarkdown() + hasIntegrations() +
           hasKOReaderSync() + hasCalibreSync() + hasBackgroundServer() + hasHomeMediaPicker() + hasWebPokedexPlugin() +
           hasEpubSupport() + hasHyphenation() + hasXtcSupport() + hasLyraTheme() + hasOtaUpdates() + hasTodoPlanner() +
           hasDarkMode() + hasBleWifiProvisioning();
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
    addFeature(hasOpenDyslexicFonts(), "opendyslexic_fonts");
    addFeature(hasImageSleep(), "image_sleep");
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
    addFeature(hasBleWifiProvisioning(), "ble_wifi_provisioning");

    return build.isEmpty() ? "lean" : build;
  }

  /**
   * JSON serialization for feature capability API.
   * Returns: {"extended_fonts":true,"image_sleep":false,...}
   */
  static String toJson() {
    String json = "{";
    json += "\"extended_fonts\":" + String(hasExtendedFonts() ? "true" : "false");
    json += ",\"opendyslexic_fonts\":" + String(hasOpenDyslexicFonts() ? "true" : "false");
    json += ",\"image_sleep\":" + String(hasImageSleep() ? "true" : "false");
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
    json += ",\"ble_wifi_provisioning\":" + String(hasBleWifiProvisioning() ? "true" : "false");
    json += "}";
    return json;
  }

  /**
   * Print compile-time feature configuration to Serial for debugging.
   * Call this during setup() to log the build configuration.
   */
  static void printToSerial() {
    Serial.println("[FEATURES] CrossPoint Reader build configuration:");
    Serial.printf("  Extended Fonts:    %s\n", hasExtendedFonts() ? "ENABLED " : "DISABLED");
    Serial.printf("  OpenDyslexic:      %s\n", hasOpenDyslexicFonts() ? "ENABLED " : "DISABLED");
    Serial.printf("  Image Sleep:       %s\n", hasImageSleep() ? "ENABLED " : "DISABLED");
    Serial.printf("  Markdown:          %s\n", hasMarkdown() ? "ENABLED " : "DISABLED");
    Serial.printf("  Integrations:      %s\n", hasIntegrations() ? "ENABLED " : "DISABLED");
    Serial.printf("  KOReader Sync:     %s\n", hasKOReaderSync() ? "ENABLED " : "DISABLED");
    Serial.printf("  Calibre Sync:      %s\n", hasCalibreSync() ? "ENABLED " : "DISABLED");
    Serial.printf("  Background Server: %s\n", hasBackgroundServer() ? "ENABLED " : "DISABLED");
    Serial.printf("  Home Media Picker: %s\n", hasHomeMediaPicker() ? "ENABLED " : "DISABLED");
    Serial.printf("  Web Pokedex:       %s\n", hasWebPokedexPlugin() ? "ENABLED " : "DISABLED");
    Serial.printf("  EPUB Support:      %s\n", hasEpubSupport() ? "ENABLED " : "DISABLED");
    Serial.printf("  Hyphenation:       %s\n", hasHyphenation() ? "ENABLED " : "DISABLED");
    Serial.printf("  XTC Support:       %s\n", hasXtcSupport() ? "ENABLED " : "DISABLED");
    Serial.printf("  Lyra Theme:        %s\n", hasLyraTheme() ? "ENABLED " : "DISABLED");
    Serial.printf("  OTA Updates:       %s\n", hasOtaUpdates() ? "ENABLED " : "DISABLED");
    Serial.printf("  Todo Planner:      %s\n", hasTodoPlanner() ? "ENABLED " : "DISABLED");
    Serial.printf("  Dark Mode:         %s\n", hasDarkMode() ? "ENABLED " : "DISABLED");
    Serial.printf("  BLE WiFi Provision: %s\n", hasBleWifiProvisioning() ? "ENABLED " : "DISABLED");
    Serial.printf("[FEATURES] %d/18 compile-time features enabled\n", enabledFeatureCount());
    Serial.printf("[FEATURES] Build: %s\n", getBuildString().c_str());
  }
};
