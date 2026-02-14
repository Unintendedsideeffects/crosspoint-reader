#pragma once
#include <string>

#include "../Activity.h"

class Bitmap;

// Call this when /sleep/ folder or sleep images are modified via web interface
void invalidateSleepImageCache();

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sleep", renderer, mappedInput) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderImageSleepScreen(const std::string& imagePath) const;
};
