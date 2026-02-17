#include "ReaderActivity.h"

#include <HalStorage.h>

#include "FeatureFlags.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "util/StringUtils.h"

#if ENABLE_EPUB_SUPPORT
#include "Epub.h"
#include "EpubReaderActivity.h"
#endif

#if ENABLE_XTC_SUPPORT
#include "Xtc.h"
#include "XtcReaderActivity.h"
#endif

#if ENABLE_MARKDOWN
#include "Markdown.h"
#include "MarkdownReaderActivity.h"
#endif

std::string ReaderActivity::extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

#if ENABLE_XTC_SUPPORT
bool ReaderActivity::isXtcFile(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch");
}
#endif

bool ReaderActivity::isTxtFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".txt"); }

bool ReaderActivity::isMarkdownFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".md"); }

#if ENABLE_EPUB_SUPPORT
std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.crosspoint"));
  if (epub->load(true, SETTINGS.embeddedStyle == 0)) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load epub");
  return nullptr;
}
#endif

#if ENABLE_XTC_SUPPORT
std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, "/.crosspoint"));
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}
#endif

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto txt = std::unique_ptr<Txt>(new Txt(path, "/.crosspoint"));
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

#if ENABLE_MARKDOWN
std::unique_ptr<Markdown> ReaderActivity::loadMarkdown(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto markdown = std::unique_ptr<Markdown>(new Markdown(path, "/.crosspoint"));
  if (markdown->load()) {
    return markdown;
  }

  Serial.printf("[%lu] [   ] Failed to load Markdown\n", millis());
  return nullptr;
}
#endif  // ENABLE_MARKDOWN

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  const auto initialPath = fromBookPath.empty() ? "/" : extractFolderPath(fromBookPath);
  onGoToLibrary(initialPath);
}

void ReaderActivity::requestGoHome() { pendingGoHome = true; }

void ReaderActivity::requestGoToLibrary(const std::string& fromBookPath) {
  pendingGoToLibrary = true;
  pendingLibraryPath = fromBookPath;
}

#if ENABLE_EPUB_SUPPORT
void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  exitActivity();
  enterNewActivity(new EpubReaderActivity(
      renderer, mappedInput, std::move(epub), [this, epubPath] { requestGoToLibrary(epubPath); },
      [this] { requestGoHome(); }));
}
#endif

#if ENABLE_XTC_SUPPORT
void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  exitActivity();
  enterNewActivity(new XtcReaderActivity(
      renderer, mappedInput, std::move(xtc), [this, xtcPath] { requestGoToLibrary(xtcPath); },
      [this] { requestGoHome(); }));
}
#endif

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  exitActivity();
  enterNewActivity(new TxtReaderActivity(
      renderer, mappedInput, std::move(txt), [this, txtPath] { requestGoToLibrary(txtPath); },
      [this] { requestGoHome(); }));
}

#if ENABLE_MARKDOWN
void ReaderActivity::onGoToMarkdownReader(std::unique_ptr<Markdown> markdown) {
  const auto mdPath = markdown->getPath();
  currentBookPath = mdPath;
  exitActivity();
  enterNewActivity(new MarkdownReaderActivity(
      renderer, mappedInput, std::move(markdown), [this, mdPath] { requestGoToLibrary(mdPath); },
      [this] { requestGoHome(); }));
}
#endif  // ENABLE_MARKDOWN

void ReaderActivity::loop() {
  ActivityWithSubactivity::loop();

  if (pendingGoHome) {
    pendingGoHome = false;
    onGoBack();
    return;
  }

  if (pendingGoToLibrary) {
    pendingGoToLibrary = false;
    const std::string path = pendingLibraryPath;
    pendingLibraryPath.clear();
    goToLibrary(path);
  }
}

void ReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  Serial.printf("[%lu] [RDR] onEnter with path: %s\n", millis(), initialBookPath.c_str());

  if (initialBookPath.empty()) {
    Serial.printf("[%lu] [RDR] Empty path, going to library\n", millis());
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;

#if ENABLE_XTC_SUPPORT
  if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else
#endif
      if (isMarkdownFile(initialBookPath)) {
#if ENABLE_MARKDOWN
    Serial.printf("[%lu] [RDR] Detected as Markdown file\n", millis());
    auto markdown = loadMarkdown(initialBookPath);
    if (!markdown) {
      Serial.printf("[%lu] [RDR] Failed to load Markdown, going back\n", millis());
      onGoBack();
      return;
    }
    Serial.printf("[%lu] [RDR] Markdown loaded, opening reader\n", millis());
    onGoToMarkdownReader(std::move(markdown));
#else
    // Markdown support not compiled in this build
    Serial.printf("[%lu] [RDR] Markdown support disabled in this build\n", millis());
    exitActivity();
    enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput,
                                                   "Markdown support\nnot available\nin this build",
                                                   EpdFontFamily::BOLD, [this] { onGoBack(); }));
#endif  // ENABLE_MARKDOWN
  } else if (isTxtFile(initialBookPath)) {
    Serial.printf("[%lu] [RDR] Detected as TXT file\n", millis());
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      Serial.printf("[%lu] [RDR] Failed to load TXT, going back\n", millis());
      onGoBack();
      return;
    }
    Serial.printf("[%lu] [RDR] TXT loaded, opening reader\n", millis());
    onGoToTxtReader(std::move(txt));
  } else {
#if ENABLE_EPUB_SUPPORT
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
#else
    Serial.printf("[%lu] [RDR] EPUB support disabled in this build\n", millis());
    exitActivity();
    enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "EPUB support\nnot available\nin this build",
                                                   EpdFontFamily::BOLD, [this] { onGoBack(); }));
#endif  // ENABLE_EPUB_SUPPORT
  }
}
