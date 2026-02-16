#include "UserFontManager.h"

#include <FeatureFlags.h>

#if ENABLE_USER_FONTS

#include <HalStorage.h>
#include <Logging.h>
#include <util/StringUtils.h>

#include <set>

#include "CrossPointSettings.h"

SdFont* UserFontManager::gRegular = nullptr;
SdFont* UserFontManager::gBold = nullptr;
SdFont* UserFontManager::gItalic = nullptr;
SdFont* UserFontManager::gBoldItalic = nullptr;

void UserFontManager::setGlobalFonts(SdFont* regular, SdFont* bold, SdFont* italic, SdFont* boldItalic) {
  gRegular = regular;
  gBold = bold;
  gItalic = italic;
  gBoldItalic = boldItalic;
}

void UserFontManager::scanFonts() {
  availableFonts.clear();
  if (!Storage.exists("/fonts")) {
    Storage.mkdir("/fonts");
    return;
  }

  std::vector<String> files = Storage.listFiles("/fonts");
  std::set<std::string> families;

  for (const auto& file : files) {
    std::string fileName = file.c_str();
    if (StringUtils::checkFileExtension(fileName, ".cpf")) {
      // Expected format: FamilyName-Style.cpf
      size_t dashPos = fileName.find_last_of('-');
      if (dashPos != std::string::npos) {
        families.insert(fileName.substr(0, dashPos));
      } else {
        // Fallback for files without a dash
        size_t dotPos = fileName.find_last_of('.');
        families.insert(fileName.substr(0, dotPos));
      }
    }
  }

  availableFonts.assign(families.begin(), families.end());

  LOG_INF("FONTS", "Scanned %d font families from SD", availableFonts.size());
}

void UserFontManager::unloadCurrentFont() {
  if (gRegular) gRegular->unload();
  if (gBold) gBold->unload();
  if (gItalic) gItalic->unload();
  if (gBoldItalic) gBoldItalic->unload();
  currentFontName = "";
}

bool UserFontManager::loadFontFamily(const std::string& fontName) {
  unloadCurrentFont();
  if (fontName.empty()) return false;

  bool anyLoaded = false;
  std::string basePath = "/fonts/" + fontName;

  if (gRegular) anyLoaded |= gRegular->load(basePath + "-Regular.cpf");
  // Try loading regular without suffix if -Regular fails
  if (gRegular && !gRegular->isLoaded()) anyLoaded |= gRegular->load(basePath + ".cpf");

  if (gBold) gBold->load(basePath + "-Bold.cpf");
  if (gItalic) gItalic->load(basePath + "-Italic.cpf");
  if (gBoldItalic) gBoldItalic->load(basePath + "-BoldItalic.cpf");

  if (anyLoaded) {
    currentFontName = fontName;
    LOG_INF("FONTS", "Loaded font family: %s", fontName.c_str());
  } else {
    LOG_ERR("FONTS", "Failed to load any font for: %s", fontName.c_str());
  }

  return anyLoaded;
}
#endif
