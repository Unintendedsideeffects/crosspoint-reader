#include "HomeActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Utf8.h>
#if ENABLE_EPUB_SUPPORT
#include <Epub.h>
#endif
#if ENABLE_XTC_SUPPORT
#include <Xtc.h>
#endif

#include <algorithm>
#include <cstring>
#include <vector>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FeatureFlags.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "ScreenComponents.h"
#include "SpiBusMutex.h"
#include "activities/TaskShutdown.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
int clampValue(const int value, const int minValue, const int maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

std::string fileNameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return path;
  }
  if (slash + 1 >= path.size()) {
    return "";
  }
  return path.substr(slash + 1);
}

const char* formatLabelFromPath(const std::string& path) {
  if (StringUtils::checkFileExtension(path, ".epub")) {
    return "EPUB";
  }
  if (StringUtils::checkFileExtension(path, ".md")) {
    return "MARKDOWN";
  }
  if (StringUtils::checkFileExtension(path, ".txt")) {
    return "TEXT";
  }
  if (StringUtils::checkFileExtension(path, ".xtch") || StringUtils::checkFileExtension(path, ".xtc")) {
    return "XTC";
  }
  return "BOOK";
}

#if ENABLE_HOME_MEDIA_PICKER
constexpr int kMediaTopPadding = 14;
constexpr int kMediaShelfTop = 48;
constexpr int kMediaBottomHintMargin = 60;
constexpr int kMenuTileHeight = 38;
constexpr int kMenuTileSpacing = 7;
constexpr int kActionsHeaderHeight = 30;
#endif
}  // namespace

void HomeActivity::taskTrampoline(void* param) {
  auto* self = static_cast<HomeActivity*>(param);
  self->displayTaskLoop();
}

#if !ENABLE_HOME_MEDIA_PICKER
int HomeActivity::getMenuItemCount() const {
  int count = 4;  // My Library, TODO, File transfer, Settings
  if (hasContinueReading) count++;
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  if (hasOpdsUrl) count++;
#endif
  return count;
}
#endif

std::string HomeActivity::fallbackTitleFromPath(const std::string& path) {
  auto title = path;
  const size_t lastSlash = title.find_last_of('/');
  if (lastSlash != std::string::npos) {
    title = title.substr(lastSlash + 1);
  }

  if (StringUtils::checkFileExtension(title, ".xtch")) {
    title.resize(title.length() - 5);
  } else if (StringUtils::checkFileExtension(title, ".epub") || StringUtils::checkFileExtension(title, ".xtc") ||
             StringUtils::checkFileExtension(title, ".txt") || StringUtils::checkFileExtension(title, ".md")) {
    title.resize(title.length() - 4);
  }

  return title;
}

std::string HomeActivity::fallbackAuthor(const RecentBook& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return "";
}

void HomeActivity::rebuildMenuLayout() {
  int idx = 0;
  menuOpenBookIndex = idx++;
  menuMyLibraryIndex = idx++;
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  menuOpdsIndex = hasOpdsUrl ? idx++ : -1;
#else
  menuOpdsIndex = -1;
#endif
  menuTodoIndex = idx++;
  menuFileTransferIndex = idx++;
  menuSettingsIndex = idx++;
  menuItemCount = idx;
}

void HomeActivity::loadRecentBooks() {
  recentBooks.clear();
  const auto& storedBooks = RECENT_BOOKS.getBooks();
  recentBooks.reserve(storedBooks.size());

  for (const auto& stored : storedBooks) {
    if (!Storage.exists(stored.path.c_str())) {
      continue;
    }

    RecentBook entry = stored;
    bool metadataChanged = false;

    if (entry.title.empty() || entry.coverBmpPath.empty()) {
      const RecentBook hydrated = RECENT_BOOKS.getDataFromBook(entry.path);
      if (!hydrated.title.empty() && hydrated.title != entry.title) {
        entry.title = hydrated.title;
        metadataChanged = true;
      }
      if (!hydrated.author.empty() && hydrated.author != entry.author) {
        entry.author = hydrated.author;
        metadataChanged = true;
      }
      if (!hydrated.coverBmpPath.empty() && hydrated.coverBmpPath != entry.coverBmpPath) {
        entry.coverBmpPath = hydrated.coverBmpPath;
        metadataChanged = true;
      }
    }

    if (entry.title.empty()) {
      entry.title = fallbackTitleFromPath(entry.path);
      if (entry.title != stored.title) {
        metadataChanged = true;
      }
    }

    if (metadataChanged) {
      RECENT_BOOKS.updateBook(entry.path, entry.title, entry.author, entry.coverBmpPath);
    }

    recentBooks.push_back(entry);
  }

  if (recentBooks.empty()) {
    selectedBookIndex = 0;
    return;
  }

  if (!APP_STATE.openEpubPath.empty()) {
    for (size_t i = 0; i < recentBooks.size(); ++i) {
      if (recentBooks[i].path == APP_STATE.openEpubPath) {
        selectedBookIndex = static_cast<int>(i);
        return;
      }
    }
  }

  selectedBookIndex = clampValue(selectedBookIndex, 0, static_cast<int>(recentBooks.size()) - 1);
}

void HomeActivity::openSelectedBook() {
  if (recentBooks.empty()) {
    return;
  }

  if (selectedBookIndex < 0 || selectedBookIndex >= static_cast<int>(recentBooks.size())) {
    selectedBookIndex = 0;
  }

  const auto& selected = recentBooks[static_cast<size_t>(selectedBookIndex)];
  if (!Storage.exists(selected.path.c_str())) {
    loadRecentBooks();
    updateRequired = true;
    return;
  }

  APP_STATE.openEpubPath = selected.path;
  APP_STATE.saveToFile();
  onContinueReading();
}

std::string HomeActivity::getMenuItemLabel(const int index) const {
  if (index == menuOpenBookIndex) {
    return recentBooks.empty() ? "Open Book (empty)" : "Open Book";
  }
  if (index == menuMyLibraryIndex) {
    return "My Library";
  }
  if (index == menuOpdsIndex) {
    return "OPDS Browser";
  }
  if (index == menuTodoIndex) {
    return "TODO";
  }
  if (index == menuFileTransferIndex) {
    return "File Transfer";
  }
  if (index == menuSettingsIndex) {
    return "Settings";
  }
  return "";
}

bool HomeActivity::drawCoverAt(const std::string& coverPath, const int x, const int y, const int width,
                               const int height) const {
  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
    return false;
  }

  SpiBusMutex::Guard guard;
  FsFile file;
  if (!Storage.openFileForRead("HOME", coverPath, file)) {
    return false;
  }

  Bitmap bitmap(file);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) {
    renderer.drawBitmap(bitmap, x, y, width, height);
  }
  file.close();
  return ok;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  exitTaskRequested.store(false);
  taskHasExited.store(false);

#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;
#else
  hasOpdsUrl = false;
#endif

#if ENABLE_HOME_MEDIA_PICKER
  loadRecentBooks();
  rebuildMenuLayout();
  selectedMenuIndex = 0;
  hasContinueReading = !recentBooks.empty();
  selectorIndex = 0;

  hasCoverImage = false;
  coverRendered = false;
  coverBmpPath.clear();
  lastBookTitle.clear();
  lastBookAuthor.clear();
#else
  // Check if we have a book to continue reading
  hasContinueReading = !APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str());

  if (hasContinueReading) {
    // Extract filename from path for display
    lastBookTitle = APP_STATE.openEpubPath;
    const size_t lastSlash = lastBookTitle.find_last_of('/');
    if (lastSlash != std::string::npos) {
      lastBookTitle = lastBookTitle.substr(lastSlash + 1);
    }

    // If epub, try to load the metadata for title/author and cover
#if ENABLE_EPUB_SUPPORT
    if (StringUtils::checkFileExtension(lastBookTitle, ".epub")) {
      Epub epub(APP_STATE.openEpubPath, "/.crosspoint");
      epub.load(false);
      if (!epub.getTitle().empty()) {
        lastBookTitle = std::string(epub.getTitle());
      }
      if (!epub.getAuthor().empty()) {
        lastBookAuthor = std::string(epub.getAuthor());
      }
      // Try to generate thumbnail image for Continue Reading card
      const int thumbHeight = renderer.getScreenHeight() / 2;
      if (epub.generateThumbBmp(thumbHeight)) {
        coverBmpPath = epub.getThumbBmpPath();
        hasCoverImage = true;
      }
    } else
#endif
#if ENABLE_XTC_SUPPORT
        if (StringUtils::checkFileExtension(lastBookTitle, ".xtch") ||
            StringUtils::checkFileExtension(lastBookTitle, ".xtc")) {
      // Handle XTC file
      Xtc xtc(APP_STATE.openEpubPath, "/.crosspoint");
      if (xtc.load()) {
        if (!xtc.getTitle().empty()) {
          lastBookTitle = std::string(xtc.getTitle());
        }
        if (!xtc.getAuthor().empty()) {
          lastBookAuthor = std::string(xtc.getAuthor());
        }
        // Try to generate thumbnail image for Continue Reading card
        const int thumbHeight = renderer.getScreenHeight() / 2;
        if (xtc.generateThumbBmp(thumbHeight)) {
          coverBmpPath = xtc.getThumbBmpPath();
          hasCoverImage = true;
        }
      }
      // Remove extension from title if we don't have metadata
      if (StringUtils::checkFileExtension(lastBookTitle, ".xtch")) {
        lastBookTitle.resize(lastBookTitle.length() - 5);
      } else if (StringUtils::checkFileExtension(lastBookTitle, ".xtc")) {
        lastBookTitle.resize(lastBookTitle.length() - 4);
      }
    } else
#endif
    {
      // No format-specific metadata available
    }
  }

  selectorIndex = 0;
#endif

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&HomeActivity::taskTrampoline, "HomeActivityTask",
              4096,               // Stack size (increased for cover image rendering)
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void HomeActivity::onExit() {
  Activity::onExit();

  TaskShutdown::requestExit(exitTaskRequested, taskHasExited, displayTaskHandle);
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  // Free the stored cover buffer if any
  freeCoverBuffer();
  recentBooks.clear();
}

#if !ENABLE_HOME_MEDIA_PICKER
bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}
#endif

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
#if ENABLE_HOME_MEDIA_PICKER
  const bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedMenuIndex == menuOpenBookIndex) {
      openSelectedBook();
      return;
    }
    if (selectedMenuIndex == menuMyLibraryIndex) {
      onMyLibraryOpen();
      return;
    }
    if (selectedMenuIndex == menuOpdsIndex) {
      onOpdsBrowserOpen();
      return;
    }
    if (selectedMenuIndex == menuTodoIndex) {
      onTodoOpen();
      return;
    }
    if (selectedMenuIndex == menuFileTransferIndex) {
      onFileTransferOpen();
      return;
    }
    if (selectedMenuIndex == menuSettingsIndex) {
      onSettingsOpen();
      return;
    }
  }

  if (!recentBooks.empty()) {
    const int bookCount = static_cast<int>(recentBooks.size());
    if (leftPressed) {
      selectedBookIndex = (selectedBookIndex + bookCount - 1) % bookCount;
      updateRequired = true;
    } else if (rightPressed) {
      selectedBookIndex = (selectedBookIndex + 1) % bookCount;
      updateRequired = true;
    }
  }

  if (menuItemCount > 0) {
    if (upPressed) {
      selectedMenuIndex = (selectedMenuIndex + menuItemCount - 1) % menuItemCount;
      updateRequired = true;
    } else if (downPressed) {
      selectedMenuIndex = (selectedMenuIndex + 1) % menuItemCount;
      updateRequired = true;
    }
  }
#else

  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    updateRequired = true;
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    updateRequired = true;
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Calculate dynamic indices based on which options are available
    int idx = 0;
    const int continueIdx = hasContinueReading ? idx++ : -1;
    const int myLibraryIdx = idx++;
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
    const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
#endif
    const int todoIdx = idx++;
    const int fileTransferIdx = idx++;
    const int settingsIdx = idx;

    if (selectorIndex == continueIdx) {
      onContinueReading();
    } else if (selectorIndex == myLibraryIdx) {
      onMyLibraryOpen();
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
    } else if (selectorIndex == opdsLibraryIdx) {
      onOpdsBrowserOpen();
#endif
    } else if (selectorIndex == todoIdx) {
      onTodoOpen();
    } else if (selectorIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (selectorIndex == settingsIdx) {
      onSettingsOpen();
    }
  }
#endif
}

void HomeActivity::displayTaskLoop() {
  while (!exitTaskRequested.load()) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      if (!exitTaskRequested.load()) {
        render();
      }
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  taskHasExited.store(true);
  vTaskDelete(nullptr);
}

void HomeActivity::render() {
#if ENABLE_HOME_MEDIA_PICKER
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  constexpr int margin = 18;
  constexpr int cardRadius = 8;
  const int menuAreaHeight = kActionsHeaderHeight + menuItemCount * kMenuTileHeight +
                             (menuItemCount > 0 ? (menuItemCount - 1) * kMenuTileSpacing : 0);
  int menuStartY = pageHeight - kMediaBottomHintMargin - menuAreaHeight + kActionsHeaderHeight;
  menuStartY = std::max(menuStartY, kMediaShelfTop + 340);

  const int shelfBottom = menuStartY - kActionsHeaderHeight - 8;
  const int shelfHeight = shelfBottom - kMediaShelfTop;

  const int eyebrowY = kMediaTopPadding;
  renderer.drawText(UI_10_FONT_ID, margin, eyebrowY, "HOME");
  const int titleY = eyebrowY + renderer.getLineHeight(UI_10_FONT_ID) + 1;
  renderer.drawText(UI_12_FONT_ID, margin, titleY, "Book Shelf", true, EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, "Book Shelf", EpdFontFamily::BOLD);
  const int dividerY = titleY + renderer.getLineHeight(UI_12_FONT_ID) / 2;
  renderer.drawLine(margin + titleWidth + 12, dividerY, pageWidth - margin, dividerY);

  const int coverHeight = clampValue(shelfHeight - 190, 190, 320);
  const int coverWidth = clampValue((coverHeight * 7) / 10, 136, 228);
  const int coverX = (pageWidth - coverWidth) / 2;
  const int coverY = kMediaShelfTop + 24;

  const int sideCoverHeight = (coverHeight * 3) / 5;
  const int sideCoverWidth = (coverWidth * 3) / 5;
  const int sideCoverY = coverY + (coverHeight - sideCoverHeight) / 2 + 10;
  const int leftCoverX = std::max(margin, coverX - sideCoverWidth + 10);
  const int rightCoverX = std::min(pageWidth - margin - sideCoverWidth, coverX + coverWidth - 10);

  auto drawPlaceholder = [&](const std::string& title, const int x, const int y, const int w, const int h,
                             const bool highlighted) {
    renderer.fillRoundedRect(x, y, w, h, cardRadius, highlighted ? Color::Black : Color::LightGray);
    renderer.drawRoundedRect(x, y, w, h, 1, cardRadius, !highlighted);

    const std::string trimmed = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), w - 18);
    const int textY = y + (h - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, textY, trimmed.c_str(), !highlighted);
  };

  auto drawBookCard = [&](const RecentBook& book, const int x, const int y, const int w, const int h, const bool front,
                          const bool selected) {
    const bool hasCover = drawCoverAt(book.coverBmpPath, x, y, w, h);
    if (!hasCover) {
      const std::string fallback = fallbackTitleFromPath(book.path);
      drawPlaceholder(fallback.empty() ? "Untitled" : fallback, x, y, w, h, selected && front);
      return;
    }

    if (!front) {
      renderer.fillRectDither(x, y, w, h, Color::LightGray);
    }

    if (selected && front) {
      renderer.drawRect(x, y, w, h, 2, true);
      renderer.drawRect(x + 6, y + 6, w - 12, h - 12);
    } else {
      renderer.drawRect(x, y, w, h);
    }
  };

  renderer.fillRectDither(margin, coverY + coverHeight - 8, pageWidth - margin * 2, 18, Color::LightGray);
  renderer.drawLine(margin, coverY + coverHeight + 10, pageWidth - margin, coverY + coverHeight + 10);

  if (recentBooks.empty()) {
    drawPlaceholder("No recent books", coverX, coverY, coverWidth, coverHeight, false);
    renderer.drawCenteredText(UI_12_FONT_ID, coverY + coverHeight + 20, "Open a file from My Library");
  } else {
    const int bookCount = static_cast<int>(recentBooks.size());
    selectedBookIndex = clampValue(selectedBookIndex, 0, bookCount - 1);

    if (bookCount > 1) {
      const int previous = (selectedBookIndex + bookCount - 1) % bookCount;
      const int next = (selectedBookIndex + 1) % bookCount;
      drawBookCard(recentBooks[static_cast<size_t>(previous)], leftCoverX, sideCoverY, sideCoverWidth, sideCoverHeight,
                   false, false);
      drawBookCard(recentBooks[static_cast<size_t>(next)], rightCoverX, sideCoverY, sideCoverWidth, sideCoverHeight,
                   false, false);
      renderer.drawText(UI_10_FONT_ID, margin, coverY + coverHeight / 2, "<");
      renderer.drawText(UI_10_FONT_ID, pageWidth - margin - renderer.getTextWidth(UI_10_FONT_ID, ">"),
                        coverY + coverHeight / 2, ">");
    }

    const auto& selectedBook = recentBooks[static_cast<size_t>(selectedBookIndex)];
    drawBookCard(selectedBook, coverX, coverY, coverWidth, coverHeight, true, true);

    std::string title = selectedBook.title.empty() ? fallbackTitleFromPath(selectedBook.path) : selectedBook.title;
    if (title.empty()) {
      title = "Untitled";
    }
    title = renderer.truncatedText(UI_12_FONT_ID, title.c_str(), pageWidth - margin * 2 - 28, EpdFontFamily::BOLD);

    std::string author = fallbackAuthor(selectedBook);
    if (author.empty()) {
      author = "Unknown author";
    } else {
      author = renderer.truncatedText(UI_10_FONT_ID, author.c_str(), pageWidth - margin * 2 - 28);
    }

    const int infoY = coverY + coverHeight + 18;
    const int infoHeight = clampValue(shelfBottom - infoY - 10, 88, 132);
    const int infoX = margin;
    const int infoWidth = pageWidth - margin * 2;

    renderer.fillRoundedRect(infoX, infoY, infoWidth, infoHeight, cardRadius, Color::LightGray);
    renderer.drawRoundedRect(infoX, infoY, infoWidth, infoHeight, 1, cardRadius, true);

    const bool isCurrentBook = selectedBook.path == APP_STATE.openEpubPath;
    const char* statusLabel = isCurrentBook ? "CURRENT BOOK" : "RECENT PICK";
    const int statusWidth = renderer.getTextWidth(UI_10_FONT_ID, statusLabel);
    const int statusX = infoX + 12;
    const int statusY = infoY + 9;
    const int statusChipWidth = statusWidth + 16;
    constexpr int statusChipHeight = 24;
    renderer.fillRoundedRect(statusX, statusY, statusWidth + 16, 24, 6, Color::Black);
    renderer.drawText(UI_10_FONT_ID, statusX + (statusChipWidth - statusWidth) / 2,
                      statusY + (statusChipHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2, statusLabel, false);

    const char* formatLabel = formatLabelFromPath(selectedBook.path);
    const int formatWidth = renderer.getTextWidth(UI_10_FONT_ID, formatLabel);
    const int formatX = infoX + infoWidth - formatWidth - 24;
    const int formatChipWidth = formatWidth + 12;
    renderer.drawRoundedRect(formatX, statusY, formatChipWidth, statusChipHeight, 1, 6, true);
    renderer.drawText(UI_10_FONT_ID, formatX + (formatChipWidth - formatWidth) / 2,
                      statusY + (statusChipHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2, formatLabel);

    const int titleTextY = statusY + 32;
    renderer.drawText(UI_12_FONT_ID, infoX + 12, titleTextY, title.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, infoX + 12, titleTextY + renderer.getLineHeight(UI_12_FONT_ID) + 2,
                      author.c_str());

    const std::string fileName =
        renderer.truncatedText(SMALL_FONT_ID, fileNameFromPath(selectedBook.path).c_str(), infoWidth - 150);
    renderer.drawText(SMALL_FONT_ID, infoX + 12, infoY + infoHeight - renderer.getLineHeight(SMALL_FONT_ID) - 8,
                      fileName.c_str());

    const std::string position =
        std::to_string(selectedBookIndex + 1) + " of " + std::to_string(static_cast<int>(recentBooks.size()));
    const int positionWidth = renderer.getTextWidth(SMALL_FONT_ID, position.c_str());
    renderer.drawText(SMALL_FONT_ID, infoX + infoWidth - positionWidth - 12,
                      infoY + infoHeight - renderer.getLineHeight(SMALL_FONT_ID) - 8, position.c_str());
  }

  renderer.drawText(UI_10_FONT_ID, margin, menuStartY - kActionsHeaderHeight + 4, "ACTIONS");

  const int menuTileWidth = pageWidth - margin * 2;
  for (int i = 0; i < menuItemCount; ++i) {
    const int tileX = margin;
    const int tileY = menuStartY + i * (kMenuTileHeight + kMenuTileSpacing);
    const bool selected = i == selectedMenuIndex;
    if (selected) {
      renderer.fillRoundedRect(tileX, tileY, menuTileWidth, kMenuTileHeight, 7, Color::Black);
    } else {
      renderer.fillRoundedRect(tileX, tileY, menuTileWidth, kMenuTileHeight, 7, Color::LightGray);
      renderer.drawRoundedRect(tileX, tileY, menuTileWidth, kMenuTileHeight, 1, 7, true);
    }

    const std::string label = renderer.truncatedText(UI_10_FONT_ID, getMenuItemLabel(i).c_str(), menuTileWidth - 52);
    const int textY = tileY + (kMenuTileHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, tileX + 14, textY, label.c_str(), !selected, EpdFontFamily::BOLD);
    if (selected) {
      renderer.drawText(UI_10_FONT_ID, tileX + menuTileWidth - 18, textY, ">", false, EpdFontFamily::BOLD);
    }
  }

  const auto labels = mappedInput.mapLabels("", "Open", "Book -", "Book +");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.drawSideButtonHints(UI_10_FONT_ID, "Menu -", "Menu +");

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const uint16_t percentage = battery.readPercentage();
  const auto percentageText = showBatteryPercentage ? std::to_string(percentage) + "%" : "";
  const int batteryX = pageWidth - 25 - renderer.getTextWidth(SMALL_FONT_ID, percentageText.c_str());
  ScreenComponents::drawBattery(renderer, batteryX, 10, showBatteryPercentage);

  renderer.displayBuffer();
#else
  // If we have a stored cover buffer, restore it instead of clearing
  const bool bufferRestored = coverBufferStored && restoreCoverBuffer();
  if (!bufferRestored) {
    renderer.clearScreen();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  constexpr int margin = 20;
  constexpr int bottomMargin = 60;

  // --- Top "book" card for the current title (selectorIndex == 0) ---
  const int bookWidth = pageWidth / 2;
  const int bookHeight = pageHeight / 2;
  const int bookX = (pageWidth - bookWidth) / 2;
  constexpr int bookY = 30;
  const bool bookSelected = hasContinueReading && selectorIndex == 0;

  // Bookmark dimensions (used in multiple places)
  const int bookmarkWidth = bookWidth / 8;
  const int bookmarkHeight = bookHeight / 5;
  const int bookmarkX = bookX + bookWidth - bookmarkWidth - 10;
  const int bookmarkY = bookY + 5;

  // Draw book card regardless, fill with message based on `hasContinueReading`
  {
    // Draw cover image as background if available (inside the box)
    // Only load from SD on first render, then use stored buffer
    if (hasContinueReading && hasCoverImage && !coverBmpPath.empty() && !coverRendered) {
      // First time: load cover from SD and render
      SpiBusMutex::Guard guard;
      FsFile file;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          // Calculate position to center image within the book card
          int coverX, coverY;

          if (bitmap.getWidth() > bookWidth || bitmap.getHeight() > bookHeight) {
            const float imgRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
            const float boxRatio = static_cast<float>(bookWidth) / static_cast<float>(bookHeight);

            if (imgRatio > boxRatio) {
              coverX = bookX;
              coverY = bookY + (bookHeight - static_cast<int>(bookWidth / imgRatio)) / 2;
            } else {
              coverX = bookX + (bookWidth - static_cast<int>(bookHeight * imgRatio)) / 2;
              coverY = bookY;
            }
          } else {
            coverX = bookX + (bookWidth - bitmap.getWidth()) / 2;
            coverY = bookY + (bookHeight - bitmap.getHeight()) / 2;
          }

          // Draw the cover image centered within the book card
          renderer.drawBitmap(bitmap, coverX, coverY, bookWidth, bookHeight);

          // Draw border around the card
          renderer.drawRect(bookX, bookY, bookWidth, bookHeight);

          // No bookmark ribbon when cover is shown - it would just cover the art

          // Store the buffer with cover image for fast navigation
          coverBufferStored = storeCoverBuffer();
          coverRendered = true;

          // First render: if selected, draw selection indicators now
          if (bookSelected) {
            renderer.drawRect(bookX + 1, bookY + 1, bookWidth - 2, bookHeight - 2);
            renderer.drawRect(bookX + 2, bookY + 2, bookWidth - 4, bookHeight - 4);
          }
        }
        file.close();
      }
    } else if (!bufferRestored && !coverRendered) {
      // No cover image: draw border or fill, plus bookmark as visual flair
      if (bookSelected) {
        renderer.fillRect(bookX, bookY, bookWidth, bookHeight);
      } else {
        renderer.drawRect(bookX, bookY, bookWidth, bookHeight);
      }

      // Draw bookmark ribbon when no cover image (visual decoration)
      if (hasContinueReading) {
        const int notchDepth = bookmarkHeight / 3;
        const int centerX = bookmarkX + bookmarkWidth / 2;

        const int xPoints[5] = {
            bookmarkX,                  // top-left
            bookmarkX + bookmarkWidth,  // top-right
            bookmarkX + bookmarkWidth,  // bottom-right
            centerX,                    // center notch point
            bookmarkX                   // bottom-left
        };
        const int yPoints[5] = {
            bookmarkY,                                // top-left
            bookmarkY,                                // top-right
            bookmarkY + bookmarkHeight,               // bottom-right
            bookmarkY + bookmarkHeight - notchDepth,  // center notch point
            bookmarkY + bookmarkHeight                // bottom-left
        };

        // Draw bookmark ribbon (inverted if selected)
        renderer.fillPolygon(xPoints, yPoints, 5, !bookSelected);
      }
    }

    // If buffer was restored, draw selection indicators if needed
    if (bufferRestored && bookSelected && coverRendered) {
      // Draw selection border (no bookmark inversion needed since cover has no bookmark)
      renderer.drawRect(bookX + 1, bookY + 1, bookWidth - 2, bookHeight - 2);
      renderer.drawRect(bookX + 2, bookY + 2, bookWidth - 4, bookHeight - 4);
    } else if (!coverRendered && !bufferRestored) {
      // Selection border already handled above in the no-cover case
    }
  }

  if (hasContinueReading) {
    // Invert text colors based on selection state:
    // - With cover: selected = white text on black box, unselected = black text on white box
    // - Without cover: selected = white text on black card, unselected = black text on white card

    // Split into words (avoid stringstream to keep this light on the MCU)
    std::vector<std::string> words;
    words.reserve(8);
    size_t pos = 0;
    while (pos < lastBookTitle.size()) {
      while (pos < lastBookTitle.size() && lastBookTitle[pos] == ' ') {
        ++pos;
      }
      if (pos >= lastBookTitle.size()) {
        break;
      }
      const size_t start = pos;
      while (pos < lastBookTitle.size() && lastBookTitle[pos] != ' ') {
        ++pos;
      }
      words.emplace_back(lastBookTitle.substr(start, pos - start));
    }

    std::vector<std::string> lines;
    std::string currentLine;
    // Extra padding inside the card so text doesn't hug the border
    const int maxLineWidth = bookWidth - 40;
    const int spaceWidth = renderer.getSpaceWidth(UI_12_FONT_ID);

    for (auto& i : words) {
      // If we just hit the line limit (3), stop processing words
      if (lines.size() >= 3) {
        // Limit to 3 lines
        // Still have words left, so add ellipsis to last line
        lines.back().append("...");

        while (!lines.back().empty() && renderer.getTextWidth(UI_12_FONT_ID, lines.back().c_str()) > maxLineWidth) {
          // Remove "..." first, then remove one UTF-8 char, then add "..." back
          lines.back().resize(lines.back().size() - 3);  // Remove "..."
          utf8RemoveLastChar(lines.back());
          lines.back().append("...");
        }
        break;
      }

      int wordWidth = renderer.getTextWidth(UI_12_FONT_ID, i.c_str());
      while (wordWidth > maxLineWidth && !i.empty()) {
        // Word itself is too long, trim it (UTF-8 safe)
        utf8RemoveLastChar(i);
        // Check if we have room for ellipsis
        std::string withEllipsis = i + "...";
        wordWidth = renderer.getTextWidth(UI_12_FONT_ID, withEllipsis.c_str());
        if (wordWidth <= maxLineWidth) {
          i = withEllipsis;
          break;
        }
      }

      int newLineWidth = renderer.getTextWidth(UI_12_FONT_ID, currentLine.c_str());
      if (newLineWidth > 0) {
        newLineWidth += spaceWidth;
      }
      newLineWidth += wordWidth;

      if (newLineWidth > maxLineWidth && !currentLine.empty()) {
        // New line too long, push old line
        lines.push_back(currentLine);
        currentLine = i;
      } else {
        currentLine.append(" ").append(i);
      }
    }

    // If lower than the line limit, push remaining words
    if (!currentLine.empty() && lines.size() < 3) {
      lines.push_back(currentLine);
    }

    // Book title text
    int totalTextHeight = renderer.getLineHeight(UI_12_FONT_ID) * static_cast<int>(lines.size());
    if (!lastBookAuthor.empty()) {
      totalTextHeight += renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2;
    }

    // Vertically center the title block within the card
    int titleYStart = bookY + (bookHeight - totalTextHeight) / 2;

    // If cover image was rendered, draw box behind title and author
    if (coverRendered) {
      constexpr int boxPadding = 8;
      // Calculate the max text width for the box
      int maxTextWidth = 0;
      for (const auto& line : lines) {
        const int lineWidth = renderer.getTextWidth(UI_12_FONT_ID, line.c_str());
        if (lineWidth > maxTextWidth) {
          maxTextWidth = lineWidth;
        }
      }
      if (!lastBookAuthor.empty()) {
        std::string trimmedAuthor = lastBookAuthor;
        while (renderer.getTextWidth(UI_10_FONT_ID, trimmedAuthor.c_str()) > maxLineWidth && !trimmedAuthor.empty()) {
          utf8RemoveLastChar(trimmedAuthor);
        }
        if (renderer.getTextWidth(UI_10_FONT_ID, trimmedAuthor.c_str()) <
            renderer.getTextWidth(UI_10_FONT_ID, lastBookAuthor.c_str())) {
          trimmedAuthor.append("...");
        }
        const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, trimmedAuthor.c_str());
        if (authorWidth > maxTextWidth) {
          maxTextWidth = authorWidth;
        }
      }

      const int boxWidth = maxTextWidth + boxPadding * 2;
      const int boxHeight = totalTextHeight + boxPadding * 2;
      const int boxX = (pageWidth - boxWidth) / 2;
      const int boxY = titleYStart - boxPadding;

      // Draw box (inverted when selected: black box instead of white)
      renderer.fillRect(boxX, boxY, boxWidth, boxHeight, bookSelected);
      // Draw border around the box (inverted when selected: white border instead of black)
      renderer.drawRect(boxX, boxY, boxWidth, boxHeight, !bookSelected);
    }

    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_12_FONT_ID, titleYStart, line.c_str(), !bookSelected);
      titleYStart += renderer.getLineHeight(UI_12_FONT_ID);
    }

    if (!lastBookAuthor.empty()) {
      titleYStart += renderer.getLineHeight(UI_10_FONT_ID) / 2;
      std::string trimmedAuthor = lastBookAuthor;
      // Trim author if too long (UTF-8 safe)
      bool wasTrimmed = false;
      while (renderer.getTextWidth(UI_10_FONT_ID, trimmedAuthor.c_str()) > maxLineWidth && !trimmedAuthor.empty()) {
        utf8RemoveLastChar(trimmedAuthor);
        wasTrimmed = true;
      }
      if (wasTrimmed && !trimmedAuthor.empty()) {
        // Make room for ellipsis
        while (renderer.getTextWidth(UI_10_FONT_ID, (trimmedAuthor + "...").c_str()) > maxLineWidth &&
               !trimmedAuthor.empty()) {
          utf8RemoveLastChar(trimmedAuthor);
        }
        trimmedAuthor.append("...");
      }
      renderer.drawCenteredText(UI_10_FONT_ID, titleYStart, trimmedAuthor.c_str(), !bookSelected);
    }

    // "Continue Reading" label at the bottom
    const int continueY = bookY + bookHeight - renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2;
    if (coverRendered) {
      // Draw box behind "Continue Reading" text (inverted when selected: black box instead of white)
      const char* continueText = "Continue Reading";
      const int continueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, continueText);
      constexpr int continuePadding = 6;
      const int continueBoxWidth = continueTextWidth + continuePadding * 2;
      const int continueBoxHeight = renderer.getLineHeight(UI_10_FONT_ID) + continuePadding;
      const int continueBoxX = (pageWidth - continueBoxWidth) / 2;
      const int continueBoxY = continueY - continuePadding / 2;
      renderer.fillRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, bookSelected);
      renderer.drawRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, !bookSelected);
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, continueText, !bookSelected);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, "Continue Reading", !bookSelected);
    }
  } else {
    // No book to continue reading
    const int y =
        bookY + (bookHeight - renderer.getLineHeight(UI_12_FONT_ID) - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, y, "No open book");
    renderer.drawCenteredText(UI_10_FONT_ID, y + renderer.getLineHeight(UI_12_FONT_ID), "Start reading below");
  }

  // --- Bottom menu tiles ---
  // Build menu items dynamically
  std::vector<const char*> menuItems = {"My Library", "TODO", "File Transfer", "Settings"};
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  if (hasOpdsUrl) {
    // Insert OPDS Browser after My Library
    menuItems.insert(menuItems.begin() + 1, "OPDS Browser");
  }
#endif

  const int menuTileWidth = pageWidth - 2 * margin;
  constexpr int menuTileHeight = 45;
  constexpr int menuSpacing = 8;
  const int totalMenuHeight =
      static_cast<int>(menuItems.size()) * menuTileHeight + (static_cast<int>(menuItems.size()) - 1) * menuSpacing;

  int menuStartY = bookY + bookHeight + 15;
  // Ensure we don't collide with the bottom button legend
  const int maxMenuStartY = pageHeight - bottomMargin - totalMenuHeight - margin;
  if (menuStartY > maxMenuStartY) {
    menuStartY = maxMenuStartY;
  }

  for (size_t i = 0; i < menuItems.size(); ++i) {
    const int overallIndex = static_cast<int>(i) + (hasContinueReading ? 1 : 0);
    constexpr int tileX = margin;
    const int tileY = menuStartY + static_cast<int>(i) * (menuTileHeight + menuSpacing);
    const bool selected = selectorIndex == overallIndex;

    if (selected) {
      renderer.fillRect(tileX, tileY, menuTileWidth, menuTileHeight);
    } else {
      renderer.drawRect(tileX, tileY, menuTileWidth, menuTileHeight);
    }

    const char* label = menuItems[i];
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    const int textX = tileX + (menuTileWidth - textWidth) / 2;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY = tileY + (menuTileHeight - lineHeight) / 2;  // vertically centered assuming y is top of text

    // Invert text when the tile is selected, to contrast with the filled background
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, !selected);
  }

  const auto labels = mappedInput.mapLabels("", "Select", "Up", "Down");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  // get percentage so we can align text properly
  const uint16_t percentage = battery.readPercentage();
  const auto percentageText = showBatteryPercentage ? std::to_string(percentage) + "%" : "";
  const auto batteryX = pageWidth - 25 - renderer.getTextWidth(SMALL_FONT_ID, percentageText.c_str());
  ScreenComponents::drawBattery(renderer, batteryX, 10, showBatteryPercentage);

  renderer.displayBuffer();
#endif
}
