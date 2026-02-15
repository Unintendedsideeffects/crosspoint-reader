#pragma once
#include <memory>

#include "../ActivityWithSubactivity.h"
#include "FeatureFlags.h"
#include "activities/home/MyLibraryActivity.h"

#if ENABLE_EPUB_SUPPORT
class Epub;
#endif
#if ENABLE_XTC_SUPPORT
class Xtc;
#endif
class Txt;

#if ENABLE_MARKDOWN
class Markdown;
#endif

class ReaderActivity final : public ActivityWithSubactivity {
  std::string initialBookPath;
  std::string currentBookPath;  // Track current book path for navigation
  const std::function<void()> onGoBack;
  const std::function<void(const std::string&)> onGoToLibrary;
#if ENABLE_EPUB_SUPPORT
  static std::unique_ptr<Epub> loadEpub(const std::string& path);
#endif
#if ENABLE_XTC_SUPPORT
  static std::unique_ptr<Xtc> loadXtc(const std::string& path);
#endif
  static std::unique_ptr<Txt> loadTxt(const std::string& path);
#if ENABLE_MARKDOWN
  static std::unique_ptr<Markdown> loadMarkdown(const std::string& path);
#endif
#if ENABLE_XTC_SUPPORT
  static bool isXtcFile(const std::string& path);
#endif
  static bool isTxtFile(const std::string& path);
  static bool isMarkdownFile(const std::string& path);

  static std::string extractFolderPath(const std::string& filePath);
  void goToLibrary(const std::string& fromBookPath = "");
#if ENABLE_EPUB_SUPPORT
  void onGoToEpubReader(std::unique_ptr<Epub> epub);
#endif
#if ENABLE_XTC_SUPPORT
  void onGoToXtcReader(std::unique_ptr<Xtc> xtc);
#endif
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
  bool isReaderActivity() const override { return true; }
};
