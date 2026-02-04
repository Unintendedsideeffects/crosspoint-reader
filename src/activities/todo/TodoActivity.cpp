#include "TodoActivity.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "SpiBusMutex.h"
#include "activities/TaskShutdown.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "fontIds.h"

namespace {
constexpr int HEADER_HEIGHT = 50;
constexpr int ITEM_HEIGHT = 35;
constexpr int MARGIN_X = 10;
constexpr int CHECKBOX_SIZE = 20;
}  // namespace

TodoActivity::TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                           std::string dateTitle, const std::function<void()>& onBack)
    : ActivityWithSubactivity("Todo", renderer, mappedInput),
      filePath(std::move(filePath)),
      dateTitle(std::move(dateTitle)),
      onBack(onBack) {}

void TodoActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  if (renderingMutex == nullptr) {
    Serial.printf("[%lu] [TODO] Failed to create rendering mutex\n", millis());
    onBack();
    return;
  }
  exitTaskRequested.store(false);
  taskHasExited.store(false);

  loadTasks();
  updateRequired.store(true);

  if (xTaskCreate(&TodoActivity::taskTrampoline, "TodoActivityTask", 4096, this, 1, &displayTaskHandle) != pdPASS) {
    Serial.printf("[%lu] [TODO] Failed to create display task\n", millis());
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
    displayTaskHandle = nullptr;
    onBack();
  }
}

void TodoActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (displayTaskHandle != nullptr) {
    TaskShutdown::requestExit(exitTaskRequested, taskHasExited, displayTaskHandle);
  }
  if (renderingMutex != nullptr) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}

void TodoActivity::taskTrampoline(void* param) {
  auto* self = static_cast<TodoActivity*>(param);
  self->displayTaskLoop();
}

void TodoActivity::displayTaskLoop() {
  while (!exitTaskRequested.load()) {
    if (updateRequired.load()) {
      updateRequired.store(false);
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      if (!exitTaskRequested.load()) {
        render();
      }
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  taskHasExited.store(true);
  vTaskDelete(nullptr);
}

void TodoActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Handle Back
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  // Capture input state before acquiring mutex
  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);

  // Guard shared state modifications with mutex to prevent race with displayTaskLoop
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  // +1 for the "Add New Task" button at the end
  const int totalItems = static_cast<int>(items.size()) + 1;
  const int visibleItems = (renderer.getScreenHeight() - HEADER_HEIGHT) / ITEM_HEIGHT;

  // Navigation (Up/Down only)
  if (upPressed) {
    if (selectedIndex > 0) {
      selectedIndex--;
      // Adjust scroll if moving above view
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      }
      updateRequired.store(true);
    }
  } else if (downPressed) {
    if (selectedIndex < totalItems - 1) {
      selectedIndex++;
      // Adjust scroll if moving below view
      if (selectedIndex >= scrollOffset + visibleItems) {
        scrollOffset = selectedIndex - visibleItems + 1;
      }
      updateRequired.store(true);
    }
  }

  // Reordering (Left/Right) - only for task items, not headers or "Add New"
  if (selectedIndex < static_cast<int>(items.size()) && !items[selectedIndex].isHeader) {
    if (leftPressed) {
      // Move task UP in list (swap with previous item, skipping headers)
      int targetIndex = selectedIndex - 1;
      while (targetIndex >= 0 && items[targetIndex].isHeader) {
        targetIndex--;
      }
      if (targetIndex >= 0) {
        std::swap(items[selectedIndex], items[targetIndex]);
        selectedIndex = targetIndex;
        if (selectedIndex < scrollOffset) {
          scrollOffset = selectedIndex;
        }
        saveTasks();
        updateRequired.store(true);
      }
    } else if (rightPressed) {
      // Move task DOWN in list (swap with next item, skipping headers)
      int targetIndex = selectedIndex + 1;
      while (targetIndex < static_cast<int>(items.size()) && items[targetIndex].isHeader) {
        targetIndex++;
      }
      if (targetIndex < static_cast<int>(items.size())) {
        std::swap(items[selectedIndex], items[targetIndex]);
        selectedIndex = targetIndex;
        if (selectedIndex >= scrollOffset + visibleItems) {
          scrollOffset = selectedIndex - visibleItems + 1;
        }
        saveTasks();
        updateRequired.store(true);
      }
    }
  }

  // Toggle / Select - handle inside mutex for toggleCurrentTask, release for addNewTask
  if (confirmReleased) {
    if (selectedIndex < static_cast<int>(items.size())) {
      toggleCurrentTask();
      xSemaphoreGive(renderingMutex);
    } else {
      xSemaphoreGive(renderingMutex);
      addNewTask();  // addNewTask manages its own mutex
    }
  } else {
    xSemaphoreGive(renderingMutex);
  }
}

void TodoActivity::loadTasks() {
  SpiBusMutex::Guard guard;
  items.clear();

  if (!SdMan.exists(filePath.c_str())) {
    // If file doesn't exist, we start empty
    return;
  }

  FsFile file = SdMan.open(filePath.c_str(), FILE_READ);
  if (!file) {
    return;
  }

  std::string line;
  char buffer[256];
  while (file.available()) {
    int len = file.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
    buffer[len] = '\0';
    // Remove CR if present
    if (len > 0 && buffer[len - 1] == '\r') {
      buffer[len - 1] = '\0';
    }
    line = buffer;

    TodoItem item;
    // Check for Markdown task format: "- [ ] " or "- [x] "
    if (line.rfind("- [ ] ", 0) == 0) {
      item.checked = false;
      item.isHeader = false;
      item.text = line.substr(6);
    } else if (line.rfind("- [x] ", 0) == 0 || line.rfind("- [X] ", 0) == 0) {
      item.checked = true;
      item.isHeader = false;
      item.text = line.substr(6);
    } else {
      // Treat as header or note
      item.checked = false;
      item.isHeader = true;
      item.text = line;
    }
    items.push_back(item);
  }
  file.close();
}

void TodoActivity::saveTasks() {
  SpiBusMutex::Guard guard;

  // Ensure directory exists
  const auto slashPos = filePath.find_last_of('/');
  if (slashPos != std::string::npos && slashPos > 0) {
    std::string dirPath = filePath.substr(0, slashPos);
    SdMan.mkdir(dirPath.c_str());
  }

  FsFile file = SdMan.open(filePath.c_str(), FILE_WRITE | O_TRUNC);
  if (!file) {
    return;
  }

  for (const auto& item : items) {
    if (item.isHeader) {
      file.println(item.text.c_str());
    } else {
      file.print("- [");
      file.print(item.checked ? "x" : " ");
      file.print("] ");
      file.println(item.text.c_str());
    }
  }
  file.close();
}

void TodoActivity::toggleCurrentTask() {
  // Note: Called with renderingMutex held by caller
  if (selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
    if (!items[selectedIndex].isHeader) {
      items[selectedIndex].checked = !items[selectedIndex].checked;
      saveTasks();
      updateRequired.store(true);
    }
  }
}

void TodoActivity::addNewTask() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();  // Hide current activity during sub-activity
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "New Task", "", 10, 0, false,
      [this](const std::string& text) {
        if (!text.empty()) {
          TodoItem newItem;
          newItem.text = text;
          newItem.checked = false;
          newItem.isHeader = false;
          items.push_back(newItem);
          saveTasks();
          // Scroll to bottom to show new task
          selectedIndex = items.size();  // Position on "Add New" button again? Or on the new task?
          // Let's position on the new task so user can see it
          selectedIndex = items.size() - 1;
          const int visibleItems = (renderer.getScreenHeight() - HEADER_HEIGHT) / ITEM_HEIGHT;
          if (selectedIndex >= scrollOffset + visibleItems) {
            scrollOffset = selectedIndex - visibleItems + 1;
          }
        }
        exitActivity();  // Close keyboard
        updateRequired.store(true);
      },
      [this]() {
        exitActivity();  // Close keyboard
        updateRequired.store(true);
      }));
  xSemaphoreGive(renderingMutex);
}

void TodoActivity::render() {
  renderer.clearScreen();

  // Header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, dateTitle.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawLine(0, HEADER_HEIGHT, renderer.getScreenWidth(), HEADER_HEIGHT);

  const int visibleItems = (renderer.getScreenHeight() - HEADER_HEIGHT) / ITEM_HEIGHT;
  const int totalItems = static_cast<int>(items.size()) + 1;  // +1 for "Add New"

  for (int i = 0; i < visibleItems; i++) {
    int itemIndex = scrollOffset + i;
    if (itemIndex >= totalItems) break;

    int y = HEADER_HEIGHT + i * ITEM_HEIGHT;
    bool isSelected = (itemIndex == selectedIndex);

    if (isSelected) {
      renderer.fillRect(0, y, renderer.getScreenWidth(), ITEM_HEIGHT);
    }

    if (itemIndex < static_cast<int>(items.size())) {
      renderItem(y, items[itemIndex], isSelected);
    } else {
      // "Add New" button
      renderer.drawCenteredText(UI_10_FONT_ID, y + 5, "+ Add New Task", !isSelected);
    }
  }

  // Hints - show reorder hints for task items
  bool canReorder = selectedIndex < static_cast<int>(items.size()) && !items[selectedIndex].isHeader;
  const auto labels = mappedInput.mapLabels("Back", "Toggle", canReorder ? "Move Up" : "", canReorder ? "Move Dn" : "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void TodoActivity::renderItem(int y, const TodoItem& item, bool isSelected) const {
  int x = MARGIN_X;

  if (!item.isHeader) {
    // Draw Checkbox
    if (isSelected) {
      // Inverted colors for selection: White border on Black background
      renderer.drawRect(x, y + 8, CHECKBOX_SIZE, CHECKBOX_SIZE, false);  // Clear rect inside black fill
      if (item.checked) {
        // Draw X or checkmark
        renderer.drawLine(x + 4, y + 12, x + 16, y + 24, false);
        renderer.drawLine(x + 16, y + 12, x + 4, y + 24, false);
      }
    } else {
      // Standard colors: Black border on White background
      renderer.drawRect(x, y + 8, CHECKBOX_SIZE, CHECKBOX_SIZE);
      if (item.checked) {
        renderer.drawLine(x + 4, y + 12, x + 16, y + 24);
        renderer.drawLine(x + 16, y + 12, x + 4, y + 24);
      }
    }
    x += CHECKBOX_SIZE + 10;
  }

  // Draw Text
  const int textY = y + (ITEM_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  // Truncate text if needed
  std::string text = item.text;
  // Simple strikethrough simulation if checked?
  // GfxRenderer doesn't support strikethrough natively easily without font support.
  // We can just draw a line over it.

  renderer.drawText(UI_10_FONT_ID, x, textY, text.c_str(), !isSelected);

  if (item.checked && !item.isHeader) {
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, text.c_str());
    int lineY = textY + renderer.getLineHeight(UI_10_FONT_ID) / 2;
    renderer.drawLine(x, lineY, x + textWidth, lineY, !isSelected);
  }
}
