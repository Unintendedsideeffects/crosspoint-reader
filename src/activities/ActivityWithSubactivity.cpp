#include "ActivityWithSubactivity.h"

void ActivityWithSubactivity::exitActivity() {
  if (!subActivity) {
    return;
  }

  if (isLoopingSubActivity) {
    pendingExit = true;
    return;
  }

  subActivity->onExit();
  subActivity.reset();
}

void ActivityWithSubactivity::applyPendingSubActivityTransition() {
  if (!pendingExit && !pendingSubActivity) {
    return;
  }

  if (subActivity) {
    subActivity->onExit();
    subActivity.reset();
  }
  pendingExit = false;

  if (pendingSubActivity) {
    subActivity = std::move(pendingSubActivity);
    subActivity->onEnter();
  }
}

void ActivityWithSubactivity::enterNewActivity(Activity* activity) {
  if (!activity) {
    return;
  }

  if (isLoopingSubActivity) {
    pendingExit = true;
    pendingSubActivity.reset(activity);
    return;
  }

  exitActivity();
  subActivity.reset(activity);
  subActivity->onEnter();
}

void ActivityWithSubactivity::loop() {
  if (subActivity) {
    isLoopingSubActivity = true;
    subActivity->loop();
    isLoopingSubActivity = false;
    applyPendingSubActivityTransition();
  }
}

void ActivityWithSubactivity::onExit() {
  Activity::onExit();
  isLoopingSubActivity = false;
  pendingExit = false;
  pendingSubActivity.reset();
  if (subActivity) {
    subActivity->onExit();
    subActivity.reset();
  }
}
