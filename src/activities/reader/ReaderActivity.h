#pragma once
#include <memory>

#include "../ActivityWithSubactivity.h"
#include "activities/home/MyLibraryActivity.h"

class Epub;
class Xtc;
class Txt;

// Feature flag for Markdown/Obsidian support
#ifndef ENABLE_MARKDOWN
#define ENABLE_MARKDOWN 1
#endif

#if ENABLE_MARKDOWN
class Markdown;
#endif

class ReaderActivity final : public ActivityWithSubactivity {
  std::string initialBookPath;
  std::string currentBookPath;  // Track current book path for navigation
  const std::function<void()> onGoBack;
  const std::function<void(const std::string&)> onGoToLibrary;
  static std::unique_ptr<Epub> loadEpub(const std::string& path);
  static std::unique_ptr<Xtc> loadXtc(const std::string& path);
  static std::unique_ptr<Txt> loadTxt(const std::string& path);
#if ENABLE_MARKDOWN
  static std::unique_ptr<Markdown> loadMarkdown(const std::string& path);
#endif
  static bool isXtcFile(const std::string& path);
  static bool isTxtFile(const std::string& path);
  static bool isMarkdownFile(const std::string& path);

  static std::string extractFolderPath(const std::string& filePath);
  void goToLibrary(const std::string& fromBookPath = "");
  void onGoToEpubReader(std::unique_ptr<Epub> epub);
  void onGoToXtcReader(std::unique_ptr<Xtc> xtc);
  void onGoToTxtReader(std::unique_ptr<Txt> txt);
#if ENABLE_MARKDOWN
  void onGoToMarkdownReader(std::unique_ptr<Markdown> markdown);
#endif

 public:
  explicit ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialBookPath,
                          const std::function<void()>& onGoBack,
                          const std::function<void(const std::string&)>& onGoToLibrary)
      : ActivityWithSubactivity("Reader", renderer, mappedInput),
        initialBookPath(std::move(initialBookPath)),
        onGoBack(onGoBack),
        onGoToLibrary(onGoToLibrary) {}
  void onEnter() override;
};
