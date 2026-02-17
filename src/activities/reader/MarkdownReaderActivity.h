#pragma once

#include <HtmlSection.h>
#include <Markdown.h>
#include <MarkdownSection.h>

#include <atomic>

#include "activities/ActivityWithSubactivity.h"

class Page;

class MarkdownReaderActivity final : public ActivityWithSubactivity {
  std::unique_ptr<Markdown> markdown;
  std::unique_ptr<MarkdownSection> mdSection;
  std::unique_ptr<HtmlSection> htmlSection;
  std::atomic<bool> useAstRenderer{false};
  int pagesUntilFullRefresh = 0;
  std::atomic<bool> htmlReady{false};
  std::atomic<bool> astReady{false};  // AST parsed for navigation
  int savedPage = 0;
  bool hasSavedPage = false;
  const std::function<void()> onGoBack;
  const std::function<void()> onGoHome;

  void render(Activity::RenderLock&& lock) override;
  void renderScreen();
  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar(int orientedMarginRight, int orientedMarginBottom, int orientedMarginLeft) const;
  void saveProgress() const;
  void loadProgress();
  bool hasActiveSection() const;
  uint16_t getActivePageCount() const;
  int getActiveCurrentPage() const;
  void setActiveCurrentPage(int page);
  std::unique_ptr<Page> loadActivePage();

  // Navigation helpers
  void jumpToNextHeading();
  void jumpToPrevHeading();
  void showTableOfContents();

 public:
  explicit MarkdownReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::unique_ptr<Markdown> markdown, const std::function<void()>& onGoBack,
                                  const std::function<void()>& onGoHome)
      : ActivityWithSubactivity("MarkdownReader", renderer, mappedInput),
        markdown(std::move(markdown)),
        onGoBack(onGoBack),
        onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
