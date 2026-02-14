#pragma once
#include <memory>

#include "Activity.h"

class ActivityWithSubactivity : public Activity {
 protected:
  std::unique_ptr<Activity> subActivity = nullptr;
  bool isLoopingSubActivity = false;
  bool pendingExit = false;
  std::unique_ptr<Activity> pendingSubActivity = nullptr;
  void exitActivity();
  void enterNewActivity(Activity* activity);
  void applyPendingSubActivityTransition();

 public:
  explicit ActivityWithSubactivity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(std::move(name), renderer, mappedInput) {}
  void loop() override;
  void onExit() override;
};
