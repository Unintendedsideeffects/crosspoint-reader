#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>

#include "activities/ActivityWithSubactivity.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public ActivityWithSubactivity {
  enum State {
    WIFI_SELECTION,
    LOADING_FEATURE_STORE,
    SELECTING_FEATURE_STORE_BUNDLE,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::atomic<bool> exitTaskRequested{false};
  std::atomic<bool> taskHasExited{false};
  bool updateRequired = false;
  const std::function<void()> goBack;
  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  size_t selectedBundleIndex = 0;
  bool usingFeatureStore = false;
  OtaUpdater updater;

  void onWifiSelectionComplete(bool success);
  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render();

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& goBack)
      : ActivityWithSubactivity("OtaUpdate", renderer, mappedInput), goBack(goBack), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  bool blocksBackgroundServer() override { return true; }
};
