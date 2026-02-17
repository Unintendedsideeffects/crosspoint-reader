#pragma once

#include <MarkdownNavigation.h>

#include <functional>
#include <vector>

#include "../Activity.h"

class TocActivity final : public Activity {
  const std::vector<TocEntry>& tocEntries;
  int selectedIndex = 0;
  int scrollOffset = 0;
  const std::function<void()> onGoBack;
  const std::function<void(size_t tocIndex)> onSelect;

  int getVisibleItems() const;
  void ensureSelectionVisible(int visibleItems);

  void renderScreen();

 public:
  explicit TocActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::vector<TocEntry>& tocEntries,
                       const std::function<void()>& onGoBack, const std::function<void(size_t tocIndex)>& onSelect)
      : Activity("Toc", renderer, mappedInput), tocEntries(tocEntries), onGoBack(onGoBack), onSelect(onSelect) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
