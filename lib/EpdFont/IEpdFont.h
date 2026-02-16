#pragma once
#include <cstdint>

#include "EpdFontData.h"

/**
 * Interface for fonts that can be used with GfxRenderer.
 * This allows for both built-in (EpdFont) and dynamic (SdFont) fonts.
 */
class IEpdFont {
 public:
  virtual ~IEpdFont() = default;

  virtual void getTextDimensions(const char* string, int* w, int* h) const = 0;
  virtual bool hasPrintableChars(const char* string) const = 0;
  virtual const EpdGlyph* getGlyph(uint32_t cp) const = 0;
  virtual const EpdFontData* getFontData() const = 0;
};
