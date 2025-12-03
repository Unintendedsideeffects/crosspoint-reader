#pragma once

#include <GxEPD2_BW.h>

#include <EpdFontRenderer.hpp>

#define XteinkDisplay GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT>

class EpdRenderer {
  XteinkDisplay* display;
  EpdFontRenderer<XteinkDisplay>* regularFont;
  EpdFontRenderer<XteinkDisplay>* boldFont;
  EpdFontRenderer<XteinkDisplay>* italicFont;
  EpdFontRenderer<XteinkDisplay>* bold_italicFont;
  EpdFontRenderer<XteinkDisplay>* smallFont;
  int marginTop;
  int marginBottom;
  int marginLeft;
  int marginRight;
  float lineCompression;
  EpdFontRenderer<XteinkDisplay>* getFontRenderer(bool bold, bool italic) const;

 public:
  explicit EpdRenderer(XteinkDisplay* display);
  ~EpdRenderer() = default;
  int getTextWidth(const char* text, bool bold = false, bool italic = false) const;
  int getSmallTextWidth(const char* text) const;
  void drawText(int x, int y, const char* text, bool bold = false, bool italic = false, uint16_t color = 1) const;
  void drawSmallText(int x, int y, const char* text, uint16_t color = 1) const;
  void drawTextBox(int x, int y, const std::string& text, int width, int height, bool bold = false,
                   bool italic = false) const;
  void drawLine(int x1, int y1, int x2, int y2, uint16_t color) const;
  void drawRect(int x, int y, int width, int height, uint16_t color) const;
  void fillRect(int x, int y, int width, int height, uint16_t color) const;
  void clearScreen(bool black = false) const;
  void flushDisplay() const;
  void flushArea(int x, int y, int width, int height) const;

  int getPageWidth() const;
  int getPageHeight() const;
  int getSpaceWidth() const;
  int getLineHeight() const;
  // set margins
  void setMarginTop(const int newMarginTop) { this->marginTop = newMarginTop; }
  void setMarginBottom(const int newMarginBottom) { this->marginBottom = newMarginBottom; }
  void setMarginLeft(const int newMarginLeft) { this->marginLeft = newMarginLeft; }
  void setMarginRight(const int newMarginRight) { this->marginRight = newMarginRight; }
  // deep sleep helper - persist any state to disk that may be needed on wake
  bool dehydrate();
  // deep sleep helper - retrieve any state from disk after wake
  bool hydrate();
  // really really clear the screen
  void reset();

  uint8_t temperature = 20;
};
