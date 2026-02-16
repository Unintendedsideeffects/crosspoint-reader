#pragma once

#include <FeatureFlags.h>
#if ENABLE_USER_FONTS

#include <string>

#include "IEpdFont.h"

class SdFont : public IEpdFont {
 public:
  SdFont();
  virtual ~SdFont();

  bool load(const std::string& path);
  void unload();

  void getTextDimensions(const char* string, int* w, int* h) const override;
  bool hasPrintableChars(const char* string) const override;
  const EpdGlyph* getGlyph(uint32_t cp) const override;
  const EpdFontData* getFontData() const override { return &fontData; }

  bool isLoaded() const { return loaded; }

 private:
  EpdFontData fontData;
  uint8_t* ownedBitmap = nullptr;
  EpdGlyph* ownedGlyphs = nullptr;
  EpdUnicodeInterval* ownedIntervals = nullptr;
  bool loaded = false;

  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;
};

#endif
