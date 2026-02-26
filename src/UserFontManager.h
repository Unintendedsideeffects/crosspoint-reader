#pragma once

#include <FeatureFlags.h>
#if ENABLE_USER_FONTS

#include <EpdFontFamily.h>
#include <SdFont.h>

#include <string>
#include <vector>

class UserFontManager {
 public:
  static UserFontManager& getInstance() {
    static UserFontManager instance;
    return instance;
  }

  void ensureScanned();
  void scanFonts();
  void invalidateCache();
  const std::vector<std::string>& getAvailableFonts() const { return availableFonts; }

  bool loadFontFamily(const std::string& fontName);
  void unloadCurrentFont();

  // Returns the font family backed by this manager's owned SdFont objects.
  // The returned pointer remains valid for the lifetime of the singleton.
  EpdFontFamily* getFontFamily() { return &fontFamily; }

 private:
  UserFontManager();

  std::vector<std::string> availableFonts;
  std::string currentFontName;
  bool fontsScanned = false;

  // SdFont objects must be declared before fontFamily so they are
  // initialized first and their addresses are stable for the initializer list.
  SdFont regularFont;
  SdFont boldFont;
  SdFont italicFont;
  SdFont boldItalicFont;
  EpdFontFamily fontFamily;
};

#endif
