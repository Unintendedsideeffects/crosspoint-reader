#pragma once

#include "activities/Activity.h"

class ClearCacheActivity final : public Activity {
 public:
  explicit ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClearCache", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, CLEARING, SUCCESS, FAILED };

  State state = WARNING;
  int clearedCount = 0;
  int failedCount = 0;

  void goBack() { finish(); }
  void clearCache();
};
