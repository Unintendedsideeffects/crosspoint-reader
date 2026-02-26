#pragma once

#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "FeatureFlags.h"
#include "UserFontManager.h"
#include "activities/settings/SettingsActivity.h"
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
#include "KOReaderCredentialStore.h"
#endif

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
inline std::vector<SettingInfo> getSettingsList() {
  std::vector<SettingInfo> list = {
      // --- Display ---
      SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                        {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM}, "sleepScreen", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_SLEEP_SOURCE, &CrossPointSettings::sleepScreenSource,
                        {StrId::STR_SLEEP, StrId::STR_POKEDEX, StrId::STR_ALL}, "sleepScreenSource",
                        StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                        {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                        {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                        "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_STATUS_BAR, &CrossPointSettings::statusBar,
                        {StrId::STR_NONE_OPT, StrId::STR_NO_PROGRESS, StrId::STR_FULL_OPT,
                         StrId::STR_BOOK_PROGRESS_PERCENTAGE, StrId::STR_PROGRESS_BAR, StrId::STR_CHAPTER_PAGE_COUNT},
                        "statusBar", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                        {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                        StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(
          StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
          "refreshFrequency", StrId::STR_CAT_DISPLAY),
      SettingInfo::DynamicEnum(
          StrId::STR_UI_THEME,
          [] {
#if ENABLE_LYRA_THEME
            return std::vector<StrId>{StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                                      StrId::STR_THEME_FORK_DRIFT};
#else
            return std::vector<StrId>{StrId::STR_THEME_CLASSIC};
#endif
          }(),
          [] { return SETTINGS.uiTheme; }, [](uint8_t v) { SETTINGS.uiTheme = v; }, "uiTheme", StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                          StrId::STR_CAT_DISPLAY),
#if ENABLE_DARK_MODE
      SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode, "darkMode", StrId::STR_CAT_DISPLAY),
#endif

      // --- Reader ---
      SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                        {
                            StrId::STR_BOOKERLY,
                            StrId::STR_NOTO_SANS,
                            StrId::STR_OPEN_DYSLEXIC,
#if ENABLE_USER_FONTS
                            StrId::STR_EXTERNAL_FONT,
#endif
                        },
                        "fontFamily", StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                        {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}, "fontSize",
                        StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                        {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing", StrId::STR_CAT_READER),
      SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5}, "screenMargin",
                         StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                        {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                         StrId::STR_BOOK_S_STYLE},
                        "paragraphAlignment", StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                          StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                          StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                        "orientation", StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing, "extraParagraphSpacing",
                          StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                          StrId::STR_CAT_READER),

      // --- Controls ---
      SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                        {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV}, "sideButtonLayout", StrId::STR_CAT_CONTROLS),
      SettingInfo::Toggle(StrId::STR_LONG_PRESS_SKIP, &CrossPointSettings::longPressChapterSkip, "longPressChapterSkip",
                          StrId::STR_CAT_CONTROLS),
      SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                        {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN}, "shortPwrBtn",
                        StrId::STR_CAT_CONTROLS),

      // --- System ---
      SettingInfo::Enum(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeout,
                        {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15, StrId::STR_MIN_30},
                        "sleepTimeout", StrId::STR_CAT_SYSTEM),
#if ENABLE_USB_MASS_STORAGE
      SettingInfo::Toggle(StrId::STR_FILE_TRANSFER, &CrossPointSettings::usbMscPromptOnConnect, "usbMscPromptOnConnect",
                          StrId::STR_CAT_SYSTEM),
#endif
#if ENABLE_BACKGROUND_SERVER
      SettingInfo::Toggle(StrId::STR_BACKGROUND_SERVER_ON_CHARGE, &CrossPointSettings::backgroundServerOnCharge,
                          "backgroundServerOnCharge", StrId::STR_CAT_SYSTEM),
#endif
  };

#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
  list.push_back(SettingInfo::DynamicString(
      StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
      [](const std::string& v) {
        KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
        KOREADER_STORE.saveToFile();
      },
      "koUsername", StrId::STR_KOREADER_SYNC));
  list.push_back(SettingInfo::DynamicString(
      StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
      [](const std::string& v) {
        KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
        KOREADER_STORE.saveToFile();
      },
      "koPassword", StrId::STR_KOREADER_SYNC));
  list.push_back(SettingInfo::DynamicString(
      StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
      [](const std::string& v) {
        KOREADER_STORE.setServerUrl(v);
        KOREADER_STORE.saveToFile();
      },
      "koServerUrl", StrId::STR_KOREADER_SYNC));
  list.push_back(SettingInfo::DynamicEnum(
      StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
      [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
      [](uint8_t v) {
        KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
        KOREADER_STORE.saveToFile();
      },
      "koMatchMethod", StrId::STR_KOREADER_SYNC));
#endif

#if ENABLE_USER_FONTS
  SettingInfo userFontPathSetting = SettingInfo::DynamicEnum(
      StrId::STR_EXTERNAL_FONT, {},
      [] {
        auto& manager = UserFontManager::getInstance();
        manager.scanFonts();
        const auto& fonts = manager.getAvailableFonts();
        if (fonts.empty()) return static_cast<uint8_t>(0);

        const std::string selectedFont = SETTINGS.userFontPath;
        const auto it = std::find(fonts.begin(), fonts.end(), selectedFont);
        if (it == fonts.end()) return static_cast<uint8_t>(0);
        return static_cast<uint8_t>(std::distance(fonts.begin(), it));
      },
      [](uint8_t value) {
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

        const size_t index = std::min(static_cast<size_t>(value), fonts.size() - 1);
        strncpy(SETTINGS.userFontPath, fonts[index].c_str(), sizeof(SETTINGS.userFontPath) - 1);
        SETTINGS.userFontPath[sizeof(SETTINGS.userFontPath) - 1] = '\0';
        if (SETTINGS.fontFamily == CrossPointSettings::USER_SD && !manager.loadFontFamily(SETTINGS.userFontPath)) {
          SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
        }
      },
      "userFontPath", StrId::STR_CAT_READER);
  userFontPathSetting.dynamicValuesGetter = [] {
    auto& manager = UserFontManager::getInstance();
    manager.scanFonts();
    return manager.getAvailableFonts();
  };
  list.push_back(std::move(userFontPathSetting));
#endif

  // --- OPDS Browser (web-only, uses CrossPointSettings char arrays) ---
  list.push_back(SettingInfo::String(StrId::STR_OPDS_SERVER_URL, SETTINGS.opdsServerUrl, sizeof(SETTINGS.opdsServerUrl),
                                     "opdsServerUrl", StrId::STR_OPDS_BROWSER));
  list.push_back(SettingInfo::String(StrId::STR_USERNAME, SETTINGS.opdsUsername, sizeof(SETTINGS.opdsUsername),
                                     "opdsUsername", StrId::STR_OPDS_BROWSER));
  list.push_back(SettingInfo::String(StrId::STR_PASSWORD, SETTINGS.opdsPassword, sizeof(SETTINGS.opdsPassword),
                                     "opdsPassword", StrId::STR_OPDS_BROWSER));

  return list;
}
