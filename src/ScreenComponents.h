#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class GfxRenderer;

class ScreenComponents {
 public:
  static const int BOOK_PROGRESS_BAR_HEIGHT = 4;

  struct TabInfo {
    const char* label;
    bool selected;
  };

  struct PopupLayout {
    int x;
    int y;
    int width;
    int height;
  };

  static void drawBattery(const GfxRenderer& renderer, int left, int top, bool showPercentage = true);
  static void drawBookProgressBar(const GfxRenderer& renderer, size_t bookProgress);

  static PopupLayout drawPopup(const GfxRenderer& renderer, const char* message);
  static void fillPopupProgress(const GfxRenderer& renderer, const PopupLayout& layout, int progress);
  static int drawTabBar(const GfxRenderer& renderer, int y, const std::vector<TabInfo>& tabs);
  static void drawScrollIndicator(const GfxRenderer& renderer, int currentPage, int totalPages, int contentTop,
                                  int contentHeight);
  static void drawProgressBar(const GfxRenderer& renderer, int x, int y, int width, int height, size_t current,
                              size_t total);
};
