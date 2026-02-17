#pragma once

#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <string>
#include <vector>

#include "../ActivityWithSubactivity.h"

struct TodoItem {
  std::string text;
  bool checked;
  bool isHeader;  // For headers/notes that aren't tasks
};

class TodoActivity final : public ActivityWithSubactivity {
 public:
  explicit TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                        std::string dateTitle, const std::function<void()>& onBack);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  std::string filePath;
  std::string dateTitle;
  const std::function<void()> onBack;

  std::vector<TodoItem> items;
  int selectedIndex = 0;
  int scrollOffset = 0;  // Index of first visible item
  bool skipInitialInput = true;

  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::atomic<bool> exitTaskRequested{false};
  std::atomic<bool> taskHasExited{false};
  std::atomic<bool> updateRequired{false};

  void loadTasks();
  void saveTasks();
  void toggleCurrentTask();
  void addNewTask();
  void showDeleteConfirmation();

  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render();
  void renderItem(int y, const TodoItem& item, bool isSelected) const;
};
