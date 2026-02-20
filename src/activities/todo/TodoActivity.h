#pragma once

#include <GfxRenderer.h>

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
  void render(Activity::RenderLock&& lock) override;

 private:
  std::string filePath;
  std::string dateTitle;
  const std::function<void()> onBack;

  std::vector<TodoItem> items;
  int selectedIndex = 0;
  int scrollOffset = 0;  // Index of first visible item
  bool skipInitialInput = true;

  void loadTasks();
  void processTaskLine(std::string& line);
  void saveTasks();
  void toggleCurrentTask();
  void addNewTask();
  void showDeleteConfirmation();

  void renderScreen();
  void renderItem(int y, const TodoItem& item, bool isSelected) const;
};
