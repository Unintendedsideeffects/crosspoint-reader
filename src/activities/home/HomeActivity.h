#pragma once
#include <functional>
#include <vector>

#include "../Activity.h"
#include "./MyLibraryActivity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
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

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
#endif
  void freeCoverBuffer();  // Free the stored cover buffer

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
  void render(Activity::RenderLock&&) override;
};
