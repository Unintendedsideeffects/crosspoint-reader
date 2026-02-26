#pragma once
#include "../ActivityWithSubactivity.h"
#include "activities/home/MyLibraryActivity.h"

class ReaderActivity final : public ActivityWithSubactivity {
  std::string initialBookPath;
  MyLibraryActivity::Tab fromTab;  // Remember which tab we came from
  bool pendingGoHome = false;
  bool pendingGoToLibrary = false;
  std::string pendingLibraryPath;
  const std::function<void()> onGoBack;
  const std::function<void(const std::string&, MyLibraryActivity::Tab)> onGoToLibrary;

  static std::string extractFolderPath(const std::string& filePath);
  void goToLibrary(const std::string& fromBookPath = "");
  void requestGoHome();
  void requestGoToLibrary(const std::string& fromBookPath = "");

 public:
  explicit ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialBookPath,
                          MyLibraryActivity::Tab fromTab, const std::function<void()>& onGoBack,
                          const std::function<void(const std::string&, MyLibraryActivity::Tab)>& onGoToLibrary)
      : ActivityWithSubactivity("Reader", renderer, mappedInput),
        initialBookPath(std::move(initialBookPath)),
        fromTab(fromTab),
        onGoBack(onGoBack),
        onGoToLibrary(onGoToLibrary) {}
  void onEnter() override;
  void loop() override;
  bool isReaderActivity() const override { return true; }
};
