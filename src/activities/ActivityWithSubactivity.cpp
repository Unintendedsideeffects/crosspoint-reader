#include "ActivityWithSubactivity.h"

void ActivityWithSubactivity::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    {
      RenderLock lock(*this);
      if (!subActivity) {
        render(std::move(lock));
      }
      // If subActivity is set, consume the notification but skip parent render
      // Note: the sub-activity will call its render() from its own display task
    }
  }
}

void ActivityWithSubactivity::exitActivity() {
  if (!subActivity) {
    return;
  }

  if (isLoopingSubActivity) {
    pendingExit = true;
    return;
  }

  // No need to lock, since onExit() already acquires its own lock
  LOG_DBG("ACT", "Exiting subactivity...");
  subActivity->onExit();
  subActivity.reset();
  pendingExit = false;

  if (pendingSubActivity) {
    subActivity = std::move(pendingSubActivity);
    subActivity->onEnter();
  }
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

  // Acquire lock to avoid 2 activities rendering at the same time during transition
  RenderLock lock(*this);
  if (subActivity) {
    subActivity->onExit();
  }
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

void ActivityWithSubactivity::requestUpdate() {
  if (!subActivity) {
    Activity::requestUpdate();
  }
  // Sub-activity should call their own requestUpdate() from their loop() function
}

void ActivityWithSubactivity::onExit() {
  // No need to lock, onExit() already acquires its own lock
  isLoopingSubActivity = false;
  pendingExit = false;
  pendingSubActivity.reset();
  if (subActivity) {
    subActivity->onExit();
    subActivity.reset();
  }
  Activity::onExit();
}
