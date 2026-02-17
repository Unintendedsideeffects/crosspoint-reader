#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class MyLibraryActivity final : public Activity {
 public:
  enum class Tab { Recent, Files };
  enum class ViewMode { List, Grid };

 private:
  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  // Recent tab state
  std::vector<RecentBook> recentBooks;

  // Files tab state (from FileSelectionActivity)
  std::string basepath = "/";
  std::vector<std::string> files;

  // Callbacks
  const std::function<void()> onGoHome;
  const std::function<void(const std::string& path, Tab fromTab)> onSelectBook;

  // Data loading
  void loadRecentBooks();
  void loadFiles();
  size_t findEntry(const std::string& name) const;

  // Rendering
  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render() const;
  void renderRecentTab() const;
  void renderFilesTab() const;
#if ENABLE_VISUAL_COVER_PICKER
  void renderGrid() const;
  void extractCovers();
  bool drawCoverAt(const std::string& path, int x, int y, int width, int height) const;

  struct GridMetrics {
    int cols;
    int rows;
    int thumbWidth;
    int thumbHeight;
    int paddingX;
    int paddingY;
    int startX;
    int startY;
  };
  GridMetrics getGridMetrics() const;
#endif

 public:
  explicit MyLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& onGoHome,
                             const std::function<void(const std::string& path, Tab fromTab)>& onSelectBook,
                             Tab initialTab = Tab::Recent, std::string initialPath = "/")
      : Activity("MyLibrary", renderer, mappedInput),
        currentTab(initialTab),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)),
        onGoHome(onGoHome),
        onSelectBook(onSelectBook) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
