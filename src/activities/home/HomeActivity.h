#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"

class HomeActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::atomic<bool> exitTaskRequested{false};
  std::atomic<bool> taskHasExited{false};
  int selectorIndex = 0;
  int selectedBookIndex = 0;
  int selectedMenuIndex = 0;
  int menuItemCount = 0;
  int menuOpenBookIndex = -1;
  int menuMyLibraryIndex = -1;
  int menuOpdsIndex = -1;
  int menuTodoIndex = -1;
  int menuFileTransferIndex = -1;
  int menuSettingsIndex = -1;
  bool updateRequired = false;
  bool hasContinueReading = false;
  bool hasOpdsUrl = false;
  bool hasCoverImage = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  std::string lastBookTitle;
  std::string lastBookAuthor;
  std::string coverBmpPath;
  std::vector<RecentBook> recentBooks;
  const std::function<void()> onContinueReading;
  const std::function<void()> onMyLibraryOpen;
  const std::function<void()> onSettingsOpen;
  const std::function<void()> onFileTransferOpen;
  const std::function<void()> onOpdsBrowserOpen;
  const std::function<void()> onTodoOpen;

  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render();
  void loadRecentBooks();
  void rebuildMenuLayout();
  void openSelectedBook();
  std::string getMenuItemLabel(int index) const;
  bool drawCoverAt(const std::string& coverPath, int x, int y, int width, int height) const;
  static std::string fallbackTitleFromPath(const std::string& path);
  static std::string fallbackAuthor(const RecentBook& book);
  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        const std::function<void()>& onContinueReading, const std::function<void()>& onMyLibraryOpen,
                        const std::function<void()>& onSettingsOpen, const std::function<void()>& onFileTransferOpen,
                        const std::function<void()>& onOpdsBrowserOpen, const std::function<void()>& onTodoOpen)
      : Activity("Home", renderer, mappedInput),
        onContinueReading(onContinueReading),
        onMyLibraryOpen(onMyLibraryOpen),
        onSettingsOpen(onSettingsOpen),
        onFileTransferOpen(onFileTransferOpen),
        onOpdsBrowserOpen(onOpdsBrowserOpen),
        onTodoOpen(onTodoOpen) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
