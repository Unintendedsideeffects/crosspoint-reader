#pragma once

#include <MarkdownNavigation.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <vector>

#include "../Activity.h"

class TocActivity final : public Activity {
  const std::vector<TocEntry>& tocEntries;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::atomic<bool> exitTaskRequested{false};
  std::atomic<bool> taskHasExited{false};
  int selectedIndex = 0;
  int scrollOffset = 0;
  bool updateRequired = false;
  const std::function<void()> onGoBack;
  const std::function<void(size_t tocIndex)> onSelect;

  int getVisibleItems() const;
  void ensureSelectionVisible(int visibleItems);

  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void renderScreen();

 public:
  explicit TocActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::vector<TocEntry>& tocEntries,
                       const std::function<void()>& onGoBack, const std::function<void(size_t tocIndex)>& onSelect)
      : Activity("Toc", renderer, mappedInput), tocEntries(tocEntries), onGoBack(onGoBack), onSelect(onSelect) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
