#include "ReaderActivity.h"

#include <HalStorage.h>

#include "Epub.h"
#include "EpubReaderActivity.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "util/StringUtils.h"

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

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
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

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  exitActivity();
  enterNewActivity(new EpubReaderActivity(
      renderer, mappedInput, std::move(epub), [this, epubPath] { goToLibrary(epubPath); }, [this] { onGoBack(); }));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  exitActivity();
  enterNewActivity(new XtcReaderActivity(
      renderer, mappedInput, std::move(xtc), [this, xtcPath] { goToLibrary(xtcPath); }, [this] { onGoBack(); }));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  exitActivity();
  enterNewActivity(new TxtReaderActivity(
      renderer, mappedInput, std::move(txt), [this, txtPath] { goToLibrary(txtPath); }, [this] { onGoBack(); }));
}

#if ENABLE_MARKDOWN
void ReaderActivity::onGoToMarkdownReader(std::unique_ptr<Markdown> markdown) {
  const auto mdPath = markdown->getPath();
  currentBookPath = mdPath;
  exitActivity();
  enterNewActivity(new MarkdownReaderActivity(
      renderer, mappedInput, std::move(markdown), [this, mdPath] { goToLibrary(mdPath); }, [this] { onGoBack(); }));
}
#endif  // ENABLE_MARKDOWN

void ReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  Serial.printf("[%lu] [RDR] onEnter with path: %s\n", millis(), initialBookPath.c_str());

  if (initialBookPath.empty()) {
    Serial.printf("[%lu] [RDR] Empty path, going to library\n", millis());
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;

  if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isMarkdownFile(initialBookPath)) {
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
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}
