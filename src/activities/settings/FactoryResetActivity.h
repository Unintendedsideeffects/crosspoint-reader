#pragma once

#include <functional>

#include "activities/ActivityWithSubactivity.h"

class FactoryResetActivity final : public ActivityWithSubactivity {
 public:
  explicit FactoryResetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::function<void()>& goBack)
      : ActivityWithSubactivity("FactoryReset", renderer, mappedInput), goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;

 private:
  enum State { WARNING, RESETTING, SUCCESS, FAILED };

  State state = WARNING;
  const std::function<void()> goBack;
  unsigned long restartAtMs = 0;

  void renderScreen();
  bool performFactoryReset();
};
