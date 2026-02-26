#include "ReaderActivity.h"

#include <HalStorage.h>
#include <Logging.h>

#include "FeatureFlags.h"
#include "SpiBusMutex.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "core/features/FeatureModules.h"
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

bool ReaderActivity::isXtcFile(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch");
}

bool ReaderActivity::isTxtFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".txt"); }

bool ReaderActivity::isMarkdownFile(const std::string& path) { return StringUtils::checkFileExtension(path, ".md"); }

#if ENABLE_EPUB_SUPPORT
std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  SpiBusMutex::Guard guard;
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.crosspoint"));
  if (epub->load()) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load epub");
  return nullptr;
}
#endif

#if ENABLE_XTC_SUPPORT
std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  SpiBusMutex::Guard guard;
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
  SpiBusMutex::Guard guard;
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
  SpiBusMutex::Guard guard;
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto markdown = std::unique_ptr<Markdown>(new Markdown(path, "/.crosspoint"));
  if (markdown->load()) {
    return markdown;
  }

  LOG_ERR("READER", "Failed to load Markdown");
  return nullptr;
}
#endif  // ENABLE_MARKDOWN

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  const auto initialPath = fromBookPath.empty() ? "/" : extractFolderPath(fromBookPath);
  onGoToLibrary(initialPath, fromTab);
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

  LOG_DBG("READER", "onEnter with path: %s", initialBookPath.c_str());

  if (initialBookPath.empty()) {
    LOG_DBG("READER", "Empty path, going to library");
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;

  const auto showUnsupported = [this](const char* logMessage, const char* uiMessage) {
    LOG_ERR("READER", "%s", logMessage);
    exitActivity();
    enterNewActivity(
        new FullScreenMessageActivity(renderer, mappedInput, uiMessage, EpdFontFamily::BOLD, [this] { onGoBack(); }));
  };

  if (isXtcFile(initialBookPath)) {
    if (!core::FeatureModules::hasCapability(core::Capability::XtcSupport)) {
      showUnsupported("XTC support disabled in this build", "XTC support\nnot available\nin this build");
      return;
    }
#if ENABLE_XTC_SUPPORT
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
    return;
#else
    showUnsupported("XTC support disabled in this build", "XTC support\nnot available\nin this build");
    return;
#endif
  }

  if (isMarkdownFile(initialBookPath)) {
    if (!core::FeatureModules::hasCapability(core::Capability::MarkdownSupport)) {
      showUnsupported("Markdown support disabled in this build", "Markdown support\nnot available\nin this build");
      return;
    }
#if ENABLE_MARKDOWN
    LOG_DBG("READER", "Detected as Markdown file");
    auto markdown = loadMarkdown(initialBookPath);
    if (!markdown) {
      LOG_ERR("READER", "Failed to load Markdown, going back");
      onGoBack();
      return;
    }
    LOG_DBG("READER", "Markdown loaded, opening reader");
    onGoToMarkdownReader(std::move(markdown));
    return;
#else
    showUnsupported("Markdown support disabled in this build", "Markdown support\nnot available\nin this build");
    return;
#endif  // ENABLE_MARKDOWN
  }

  if (isTxtFile(initialBookPath)) {
    LOG_DBG("READER", "Detected as TXT file");
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      LOG_ERR("READER", "Failed to load TXT, going back");
      onGoBack();
      return;
    }
    LOG_DBG("READER", "TXT loaded, opening reader");
    onGoToTxtReader(std::move(txt));
    return;
  }

  if (!core::FeatureModules::hasCapability(core::Capability::EpubSupport)) {
    showUnsupported("EPUB support disabled in this build", "EPUB support\nnot available\nin this build");
    return;
  }

#if ENABLE_EPUB_SUPPORT
  auto epub = loadEpub(initialBookPath);
  if (!epub) {
    onGoBack();
    return;
  }
  onGoToEpubReader(std::move(epub));
#else
  showUnsupported("EPUB support disabled in this build", "EPUB support\nnot available\nin this build");
  return;
#endif  // ENABLE_EPUB_SUPPORT
}
