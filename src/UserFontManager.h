#pragma once

#include <FeatureFlags.h>
#if ENABLE_USER_FONTS

#include <SdFont.h>

#include <string>
#include <vector>

class UserFontManager {
 public:
  static UserFontManager& getInstance() {
    static UserFontManager instance;
    return instance;
  }

  void scanFonts();
  const std::vector<std::string>& getAvailableFonts() const { return availableFonts; }

  bool loadFontFamily(const std::string& fontName);
  void unloadCurrentFont();

  // Accessors for global SdFonts (defined in main.cpp)
  static void setGlobalFonts(SdFont* regular, SdFont* bold, SdFont* italic, SdFont* boldItalic);

 private:
  UserFontManager() = default;
  std::vector<std::string> availableFonts;
  std::string currentFontName;

  static SdFont* gRegular;
  static SdFont* gBold;
  static SdFont* gItalic;
  static SdFont* gBoldItalic;
};

#endif
