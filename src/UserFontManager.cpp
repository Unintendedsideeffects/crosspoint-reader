#include "UserFontManager.h"

#include <FeatureFlags.h>

#if ENABLE_USER_FONTS

#include <HalStorage.h>
#include <Logging.h>
#include <util/StringUtils.h>

#include <cstring>
#include <set>

#include "CrossPointSettings.h"

namespace {
bool hasSuffix(const std::string& value, const char* suffix) {
  const size_t suffixLen = strlen(suffix);
  return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}
}  // namespace

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
    const size_t slashPos = fileName.find_last_of('/');
    if (slashPos != std::string::npos) {
      fileName = fileName.substr(slashPos + 1);
    }

    if (StringUtils::checkFileExtension(fileName, ".cpf")) {
      const std::string baseName = fileName.substr(0, fileName.size() - 4);
      if (hasSuffix(baseName, "-Regular")) {
        families.insert(baseName.substr(0, baseName.size() - 8));
      } else if (hasSuffix(baseName, "-Bold")) {
        families.insert(baseName.substr(0, baseName.size() - 5));
      } else if (hasSuffix(baseName, "-Italic")) {
        families.insert(baseName.substr(0, baseName.size() - 7));
      } else if (hasSuffix(baseName, "-BoldItalic")) {
        families.insert(baseName.substr(0, baseName.size() - 11));
      } else {
        families.insert(baseName);
        if (baseName.find('-') != std::string::npos) {
          LOG_WRN("FONTS", "Non-canonical user font filename detected: %s", fileName.c_str());
        }
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

  const std::string basePath = "/fonts/" + fontName;
  std::string regularPath;

  if (!gRegular) return false;

  const std::string canonicalRegular = basePath + "-Regular.cpf";
  const std::string fallbackRegular = basePath + ".cpf";
  if (Storage.exists(canonicalRegular.c_str()) && gRegular->load(canonicalRegular)) {
    regularPath = canonicalRegular;
  } else if (Storage.exists(fallbackRegular.c_str()) && gRegular->load(fallbackRegular)) {
    regularPath = fallbackRegular;
  }

  if (regularPath.empty()) {
    LOG_ERR("FONTS", "Missing or invalid regular font for family: %s", fontName.c_str());
    return false;
  }

  auto loadStyleOrFallback = [&](SdFont* target, const std::string& stylePath) {
    if (!target) return;
    if (Storage.exists(stylePath.c_str())) {
      if (target->load(stylePath)) return;
      LOG_WRN("FONTS", "Failed loading style file, falling back to regular: %s", stylePath.c_str());
    }
    target->load(regularPath);
  };

  loadStyleOrFallback(gBold, basePath + "-Bold.cpf");
  loadStyleOrFallback(gItalic, basePath + "-Italic.cpf");
  loadStyleOrFallback(gBoldItalic, basePath + "-BoldItalic.cpf");

  currentFontName = fontName;
  LOG_INF("FONTS", "Loaded font family: %s", fontName.c_str());
  return true;
}
#endif
