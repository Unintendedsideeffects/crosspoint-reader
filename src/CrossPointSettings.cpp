#include "CrossPointSettings.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <cstring>

#include "FeatureFlags.h"
#include "SpiBusMutex.h"
#include "fontIds.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

void readAndValidate(FsFile& file, uint8_t& member, const uint8_t maxValue) {
  uint8_t tempValue;
  serialization::readPod(file, tempValue);
  if (tempValue < maxValue) {
    member = tempValue;
  }
}

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 2;
// Increment this when adding new persisted settings fields
constexpr uint8_t SETTINGS_COUNT = 31;
constexpr char SETTINGS_FILE[] = "/.crosspoint/settings.bin";

// Validate front button mapping to ensure each hardware button is unique.
// If duplicates are detected, reset to the default physical order to prevent invalid mappings.
void validateFrontButtonMapping(CrossPointSettings& settings) {
  // Snapshot the logical->hardware mapping so we can compare for duplicates.
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        // Duplicate detected: restore the default physical order (Back, Confirm, Left, Right).
        settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
        settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
        settings.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
        settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

// Convert legacy front button layout into explicit logical->hardware mapping.
void applyLegacyFrontButtonLayout(CrossPointSettings& settings) {
  settings.applyFrontButtonLayoutPreset(
      static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(settings.frontButtonLayout));
}
}  // namespace

bool CrossPointSettings::saveToFile() const {
  SpiBusMutex::Guard guard;
  // Make sure the directory exists
  Storage.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!Storage.openFileForWrite("CPS", SETTINGS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, SETTINGS_COUNT);
  serialization::writePod(outputFile, sleepScreen);
  serialization::writePod(outputFile, extraParagraphSpacing);
  serialization::writePod(outputFile, shortPwrBtn);
  serialization::writePod(outputFile, statusBar);
  serialization::writePod(outputFile, orientation);
  serialization::writePod(outputFile, frontButtonLayout);  // legacy
  serialization::writePod(outputFile, sideButtonLayout);
  serialization::writePod(outputFile, fontFamily);
  serialization::writePod(outputFile, fontSize);
  serialization::writePod(outputFile, lineSpacing);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, sleepTimeout);
  serialization::writePod(outputFile, refreshFrequency);
  serialization::writePod(outputFile, screenMargin);
  serialization::writePod(outputFile, sleepScreenCoverMode);
  serialization::writeString(outputFile, std::string(opdsServerUrl));
  serialization::writePod(outputFile, textAntiAliasing);
  serialization::writePod(outputFile, hideBatteryPercentage);
  serialization::writePod(outputFile, longPressChapterSkip);
  serialization::writePod(outputFile, hyphenationEnabled);
  serialization::writeString(outputFile, std::string(opdsUsername));
  serialization::writeString(outputFile, std::string(opdsPassword));
  serialization::writePod(outputFile, sleepScreenCoverFilter);
  serialization::writePod(outputFile, backgroundServerOnCharge);
  serialization::writePod(outputFile, todoFallbackCover);
  serialization::writePod(outputFile, timeMode);
  serialization::writePod(outputFile, timeZoneOffset);
  serialization::writePod(outputFile, lastTimeSyncEpoch);
  serialization::writePod(outputFile, releaseChannel);
  serialization::writePod(outputFile, sleepScreenSource);
  serialization::writeString(outputFile, std::string(userFontPath));
  // New fields added at end for backward compatibility
  outputFile.close();

  LOG_DBG("CPS", "Settings saved to file");
  return true;
}

bool CrossPointSettings::loadFromFile() {
  SpiBusMutex::Guard guard;
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", SETTINGS_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version < 1 || version > SETTINGS_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  // load settings that exist (support older files with fewer fields)
  uint8_t settingsRead = 0;
  // Track whether remap fields were present in the settings file.
  bool frontButtonMappingRead = false;
  do {
    readAndValidate(inputFile, sleepScreen, SLEEP_SCREEN_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, shortPwrBtn, SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, statusBar, STATUS_BAR_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, orientation, ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonLayout, FRONT_BUTTON_LAYOUT_COUNT);  // legacy
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, fontFamily, FONT_FAMILY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, fontSize, FONT_SIZE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, lineSpacing, LINE_COMPRESSION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, paragraphAlignment, PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepTimeout, SLEEP_TIMEOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, refreshFrequency, REFRESH_FREQUENCY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverMode, SLEEP_SCREEN_COVER_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string urlStr;
      serialization::readString(inputFile, urlStr);
      strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
      opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, hideBatteryPercentage, HIDE_BATTERY_PERCENTAGE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, longPressChapterSkip);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string usernameStr;
      serialization::readString(inputFile, usernameStr);
      strncpy(opdsUsername, usernameStr.c_str(), sizeof(opdsUsername) - 1);
      opdsUsername[sizeof(opdsUsername) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string passwordStr;
      serialization::readString(inputFile, passwordStr);
      strncpy(opdsPassword, passwordStr.c_str(), sizeof(opdsPassword) - 1);
      opdsPassword[sizeof(opdsPassword) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverFilter, SLEEP_SCREEN_COVER_FILTER_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, backgroundServerOnCharge);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, todoFallbackCover);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, timeMode);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, timeZoneOffset);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, lastTimeSyncEpoch);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, releaseChannel, RELEASE_CHANNEL_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenSource, SLEEP_SCREEN_SOURCE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    if (version >= 2) {
      std::string pathStr;
      serialization::readString(inputFile, pathStr);
      strncpy(userFontPath, pathStr.c_str(), sizeof(userFontPath) - 1);
      userFontPath[sizeof(userFontPath) - 1] = '\0';
      if (++settingsRead >= fileSettingsCount) break;
    }
    // New fields added at end for backward compatibility
  } while (false);

  if (frontButtonMappingRead) {
    validateFrontButtonMapping(*this);
  } else {
    applyLegacyFrontButtonLayout(*this);
  }

  validateAndClamp();
  inputFile.close();
  LOG_DBG("CPS", "Settings loaded from file");
  return true;
}

void CrossPointSettings::applyFrontButtonLayoutPreset(const FRONT_BUTTON_LAYOUT layout) {
  frontButtonLayout = static_cast<uint8_t>(layout);

  switch (layout) {
    case LEFT_RIGHT_BACK_CONFIRM:
      frontButtonBack = FRONT_HW_LEFT;
      frontButtonConfirm = FRONT_HW_RIGHT;
      frontButtonLeft = FRONT_HW_BACK;
      frontButtonRight = FRONT_HW_CONFIRM;
      break;
    case LEFT_BACK_CONFIRM_RIGHT:
      frontButtonBack = FRONT_HW_CONFIRM;
      frontButtonConfirm = FRONT_HW_LEFT;
      frontButtonLeft = FRONT_HW_BACK;
      frontButtonRight = FRONT_HW_RIGHT;
      break;
    case BACK_CONFIRM_RIGHT_LEFT:
      frontButtonBack = FRONT_HW_BACK;
      frontButtonConfirm = FRONT_HW_CONFIRM;
      frontButtonLeft = FRONT_HW_RIGHT;
      frontButtonRight = FRONT_HW_LEFT;
      break;
    case LEFT_LEFT_RIGHT_RIGHT:
    case BACK_CONFIRM_LEFT_RIGHT:
    default:
      // LEFT_LEFT_RIGHT_RIGHT uses dedicated runtime behavior in MappedInputManager.
      // Keep the underlying one-to-one mapping in default hardware order.
      frontButtonBack = FRONT_HW_BACK;
      frontButtonConfirm = FRONT_HW_CONFIRM;
      frontButtonLeft = FRONT_HW_LEFT;
      frontButtonRight = FRONT_HW_RIGHT;
      break;
  }
}

void CrossPointSettings::enforceButtonLayoutConstraints() {
  // In dual-side mode, short power taps are reserved for Confirm/Back emulation.
  if (frontButtonLayout == LEFT_LEFT_RIGHT_RIGHT) {
    shortPwrBtn = SELECT;
  }
}

void CrossPointSettings::validateAndClamp() {
  // Enum bounds - clamp to valid range, reset to default if out of bounds
  if (sleepScreen >= SLEEP_SCREEN_MODE_COUNT) sleepScreen = DARK;
  if (sleepScreenCoverMode > CROP) sleepScreenCoverMode = FIT;
  if (sleepScreenSource >= SLEEP_SCREEN_SOURCE_COUNT) sleepScreenSource = SLEEP_SOURCE_SLEEP;
  if (statusBar > FULL) statusBar = FULL;
  if (orientation > LANDSCAPE_CCW) orientation = PORTRAIT;
  if (frontButtonLayout > LEFT_LEFT_RIGHT_RIGHT) frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  if (sideButtonLayout > NEXT_PREV) sideButtonLayout = PREV_NEXT;
  if (fontFamily >= FONT_FAMILY_COUNT) fontFamily = BOOKERLY;
  if (fontSize > EXTRA_LARGE) fontSize = MEDIUM;
#if !ENABLE_EXTENDED_FONTS
  if (fontFamily != USER_SD) fontFamily = BOOKERLY;
  fontSize = MEDIUM;
#elif !ENABLE_OPENDYSLEXIC_FONTS
  if (fontFamily == OPENDYSLEXIC) fontFamily = NOTOSANS;
#endif
#if !ENABLE_USER_FONTS
  if (fontFamily == USER_SD) fontFamily = BOOKERLY;
#endif
  if (lineSpacing > WIDE) lineSpacing = NORMAL;
  if (paragraphAlignment > RIGHT_ALIGN) paragraphAlignment = JUSTIFIED;
  if (sleepTimeout > SLEEP_30_MIN) sleepTimeout = SLEEP_10_MIN;
  if (refreshFrequency > REFRESH_30) refreshFrequency = REFRESH_15;
  if (shortPwrBtn > SELECT) shortPwrBtn = IGNORE;
  if (hideBatteryPercentage > HIDE_ALWAYS) hideBatteryPercentage = HIDE_NEVER;
  if (timeMode > TIME_MANUAL) timeMode = TIME_UTC;
  if (todoFallbackCover > TODO_FALLBACK_NONE) todoFallbackCover = TODO_FALLBACK_STANDARD;
  if (releaseChannel >= RELEASE_CHANNEL_COUNT) releaseChannel = RELEASE_STABLE;

  // Range values
  // timeZoneOffset: 0 = UTC-12, 12 = UTC+0, 26 = UTC+14
  if (timeZoneOffset > 26) timeZoneOffset = 12;  // Reset to UTC+0
  // screenMargin: valid range 5-40
  if (screenMargin < 5 || screenMargin > 40) screenMargin = 5;

  // Boolean values - normalize to 0 or 1
  extraParagraphSpacing = extraParagraphSpacing ? 1 : 0;
  textAntiAliasing = textAntiAliasing ? 1 : 0;
  hyphenationEnabled = hyphenationEnabled ? 1 : 0;
  longPressChapterSkip = longPressChapterSkip ? 1 : 0;
  backgroundServerOnCharge = backgroundServerOnCharge ? 1 : 0;

  enforceButtonLayoutConstraints();
}

float CrossPointSettings::getReaderLineCompression() const {
  switch (fontFamily) {
    case BOOKERLY:
    default:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
      }
    case NOTOSANS:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case OPENDYSLEXIC:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case USER_SD:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
      }
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  switch (sleepTimeout) {
    case SLEEP_1_MIN:
      return 1UL * 60 * 1000;
    case SLEEP_5_MIN:
      return 5UL * 60 * 1000;
    case SLEEP_10_MIN:
    default:
      return 10UL * 60 * 1000;
    case SLEEP_15_MIN:
      return 15UL * 60 * 1000;
    case SLEEP_30_MIN:
      return 30UL * 60 * 1000;
  }
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
}

int CrossPointSettings::getTimeZoneOffsetSeconds() const {
  const int offsetHours = static_cast<int>(timeZoneOffset) - 12;
  return offsetHours * 3600;
}

int CrossPointSettings::getReaderFontId() const {
#if !ENABLE_EXTENDED_FONTS
  return BOOKERLY_14_FONT_ID;
#else
  uint8_t effectiveFamily = fontFamily;
#if !ENABLE_OPENDYSLEXIC_FONTS
  if (effectiveFamily == OPENDYSLEXIC) effectiveFamily = NOTOSANS;
#endif

  switch (effectiveFamily) {
    case USER_SD:
      return USER_SD_FONT_ID;
    case BOOKERLY:
    default:
      switch (fontSize) {
        case SMALL:
          return BOOKERLY_12_FONT_ID;
        case MEDIUM:
        default:
          return BOOKERLY_14_FONT_ID;
        case LARGE:
          return BOOKERLY_16_FONT_ID;
        case EXTRA_LARGE:
          return BOOKERLY_18_FONT_ID;
      }
    case NOTOSANS:
      switch (fontSize) {
        case SMALL:
          return NOTOSANS_12_FONT_ID;
        case MEDIUM:
        default:
          return NOTOSANS_14_FONT_ID;
        case LARGE:
          return NOTOSANS_16_FONT_ID;
        case EXTRA_LARGE:
          return NOTOSANS_18_FONT_ID;
      }
    case OPENDYSLEXIC:
      switch (fontSize) {
        case SMALL:
          return OPENDYSLEXIC_8_FONT_ID;
        case MEDIUM:
        default:
          return OPENDYSLEXIC_10_FONT_ID;
        case LARGE:
          return OPENDYSLEXIC_12_FONT_ID;
        case EXTRA_LARGE:
          return OPENDYSLEXIC_14_FONT_ID;
      }
  }
#endif
}
