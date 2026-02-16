#pragma once
#include "IEpdFont.h"

class EpdFont : public IEpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;

 public:
  const EpdFontData* data;
  explicit EpdFont(const EpdFontData* data) : data(data) {}
  virtual ~EpdFont() = default;

  void getTextDimensions(const char* string, int* w, int* h) const override;
  bool hasPrintableChars(const char* string) const override;
  const EpdGlyph* getGlyph(uint32_t cp) const override;
  const EpdFontData* getFontData() const override { return data; }
};
