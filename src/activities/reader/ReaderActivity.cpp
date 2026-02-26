#include "ReaderActivity.h"

#include <Logging.h>

#include "activities/util/FullScreenMessageActivity.h"
#include "core/features/FeatureModules.h"

std::string ReaderActivity::extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

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

  const auto showUnsupported = [this](const char* logMessage, const char* uiMessage) {
    LOG_ERR("READER", "%s", logMessage);
    exitActivity();
    enterNewActivity(
        new FullScreenMessageActivity(renderer, mappedInput, uiMessage, EpdFontFamily::BOLD, [this] { onGoBack(); }));
  };

  const auto result = core::FeatureModules::createReaderActivityForPath(
      initialBookPath, renderer, mappedInput, [this](const std::string& path) { requestGoToLibrary(path); },
      [this] { requestGoHome(); });

  switch (result.status) {
    case core::FeatureModules::ReaderOpenStatus::Opened:
      exitActivity();
      enterNewActivity(result.activity);
      return;
    case core::FeatureModules::ReaderOpenStatus::Unsupported:
      showUnsupported(result.logMessage ? result.logMessage : "Reader format disabled in this build",
                      result.uiMessage ? result.uiMessage : "Reader format\nnot available\nin this build");
      return;
    case core::FeatureModules::ReaderOpenStatus::LoadFailed:
      onGoBack();
      return;
  }

  onGoBack();
}
