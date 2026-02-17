#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

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
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // My Library, TODO, File transfer, Settings
  if (hasContinueReading) count++;
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  if (hasOpdsUrl) count++;
#endif
  return count;
}

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
    if (entry.title.empty()) {
      entry.title = fallbackTitleFromPath(entry.path);
      if (entry.title != stored.title) {
        RECENT_BOOKS.updateBook(entry.path, entry.title, entry.author, entry.coverBmpPath);
      }
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

  selectedBookIndex = std::min(selectedBookIndex, static_cast<int>(recentBooks.size()) - 1);
  selectedBookIndex = std::max(0, selectedBookIndex);
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (size_t i = 0; i < recentBooks.size(); ++i) {
    RecentBook& book = recentBooks[i];
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
#if ENABLE_EPUB_SUPPORT
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
#endif
        } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                   StringUtils::checkFileExtension(book.path, ".xtc")) {
#if ENABLE_XTC_SUPPORT
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
#endif
        }
      }
    }
    progress++;
  }
  recentsLoading = false;
  recentsLoaded = true;
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
    requestUpdate();
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

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

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
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
  recentBooks.clear();
}

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
  coverBufferStored = true;
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
      requestUpdate();
    } else if (rightPressed) {
      selectedBookIndex = (selectedBookIndex + 1) % bookCount;
      requestUpdate();
    }
  }

  if (menuItemCount > 0) {
    if (upPressed) {
      selectedMenuIndex = (selectedMenuIndex + menuItemCount - 1) % menuItemCount;
      requestUpdate();
    } else if (downPressed) {
      selectedMenuIndex = (selectedMenuIndex + 1) % menuItemCount;
      requestUpdate();
    }
  }
#else

  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
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

void HomeActivity::render(Activity::RenderLock&& lock) {
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // If we are using the new media picker UI, use its specialized rendering
#if ENABLE_HOME_MEDIA_PICKER
  GUI.drawRecentBookCover(renderer, Rect(0, 0, pageWidth, pageHeight), recentBooks, selectedBookIndex, coverRendered,
                          coverBufferStored, bufferRestored, [this]() { return storeCoverBuffer(); });

  // Calculate menu layout
  const int menuTileWidth = (pageWidth - 40);
  const int menuTileHeight = 45;
  const int menuSpacing = 10;
  const int menuStartY = metrics.homeCoverHeight + 20;

  std::vector<const char*> menuItems;
  menuItems.push_back(recentBooks.empty() ? "Open Book (empty)" : "Open Book");
  menuItems.push_back("My Library");
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  if (hasOpdsUrl) menuItems.push_back("OPDS Browser");
#endif
  menuItems.push_back("TODO");
  menuItems.push_back("File Transfer");
  menuItems.push_back("Settings");

  for (size_t i = 0; i < menuItems.size(); ++i) {
    const int tileY = menuStartY + i * (menuTileHeight + menuSpacing);
    const bool selected = (static_cast<int>(i) == selectedMenuIndex);

    if (selected) {
      renderer.fillRect(20, tileY, menuTileWidth, menuTileHeight);
    } else {
      renderer.drawRect(20, tileY, menuTileWidth, menuTileHeight);
    }

    renderer.drawCenteredText(UI_10_FONT_ID, tileY + (menuTileHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                              menuItems[i], !selected);
  }

  const auto labels = mappedInput.mapLabels("", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

#else
  constexpr int margin = 20;

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
    }
  }

  if (hasContinueReading) {
    // Split into words
    std::vector<std::string> words;
    size_t pos = 0;
    while (pos < lastBookTitle.size()) {
      while (pos < lastBookTitle.size() && lastBookTitle[pos] == ' ') ++pos;
      if (pos >= lastBookTitle.size()) break;
      size_t start = pos;
      while (pos < lastBookTitle.size() && lastBookTitle[pos] != ' ') ++pos;
      words.push_back(lastBookTitle.substr(start, pos - start));
    }

    std::vector<std::string> lines;
    std::string currentLine;
    const int maxLineWidth = bookWidth - 40;
    const int spaceWidth = renderer.getSpaceWidth(UI_12_FONT_ID);

    for (auto& word : words) {
      if (lines.size() >= 3) {
        lines.back() += "...";
        break;
      }
      int wordWidth = renderer.getTextWidth(UI_12_FONT_ID, word.c_str());
      if (wordWidth > maxLineWidth) {
        while (renderer.getTextWidth(UI_12_FONT_ID, (word + "...").c_str()) > maxLineWidth && !word.empty()) {
          utf8RemoveLastChar(word);
        }
        word += "...";
      }

      int curWidth = renderer.getTextWidth(UI_12_FONT_ID, currentLine.c_str());
      if (!currentLine.empty() && curWidth + spaceWidth + wordWidth > maxLineWidth) {
        lines.push_back(currentLine);
        currentLine = word;
      } else {
        if (!currentLine.empty()) currentLine += " ";
        currentLine += word;
      }
    }
    if (!currentLine.empty() && lines.size() < 3) lines.push_back(currentLine);

    int totalTextHeight = renderer.getLineHeight(UI_12_FONT_ID) * lines.size();
    if (!lastBookAuthor.empty()) totalTextHeight += renderer.getLineHeight(UI_10_FONT_ID) * 1.5;

    int titleYStart = bookY + (bookHeight - totalTextHeight) / 2;

    if (coverRendered) {
      // Draw background box for text legibility over cover
      int maxW = 0;
      for (auto& l : lines) maxW = std::max(maxW, renderer.getTextWidth(UI_12_FONT_ID, l.c_str()));
      int boxW = maxW + 16;
      int boxH = totalTextHeight + 16;
      renderer.fillRect((pageWidth - boxW) / 2, titleYStart - 8, boxW, boxH, bookSelected);
      renderer.drawRect((pageWidth - boxW) / 2, titleYStart - 8, boxW, boxH, !bookSelected);
    }

    for (auto& l : lines) {
      renderer.drawCenteredText(UI_12_FONT_ID, titleYStart, l.c_str(), !bookSelected);
      titleYStart += renderer.getLineHeight(UI_12_FONT_ID);
    }

    if (!lastBookAuthor.empty()) {
      titleYStart += renderer.getLineHeight(UI_10_FONT_ID) * 0.5;
      std::string author = lastBookAuthor;
      if (renderer.getTextWidth(UI_10_FONT_ID, author.c_str()) > maxLineWidth) {
        while (renderer.getTextWidth(UI_10_FONT_ID, (author + "...").c_str()) > maxLineWidth && !author.empty()) {
          utf8RemoveLastChar(author);
        }
        author += "...";
      }
      renderer.drawCenteredText(UI_10_FONT_ID, titleYStart, author.c_str(), !bookSelected);
    }

    const int continueY = bookY + bookHeight - renderer.getLineHeight(UI_10_FONT_ID) * 1.5;
    renderer.drawCenteredText(UI_10_FONT_ID, continueY, "Continue Reading", !bookSelected);
  } else {
    int y = bookY + (bookHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, y, "No open book");
  }

  // Draw other menu items
  int menuStartY = bookY + bookHeight + 30;
  int menuTileWidth = pageWidth - 40;
  int menuTileHeight = 45;
  int menuSpacing = 10;

  std::vector<const char*> labels_text = {"My Library", "TODO", "File Transfer", "Settings"};
  for (size_t i = 0; i < labels_text.size(); ++i) {
    int tileY = menuStartY + i * (menuTileHeight + menuSpacing);
    bool selected = (selectorIndex == (int)i + (hasContinueReading ? 1 : 0));
    if (selected)
      renderer.fillRect(20, tileY, menuTileWidth, menuTileHeight);
    else
      renderer.drawRect(20, tileY, menuTileWidth, menuTileHeight);
    renderer.drawCenteredText(UI_10_FONT_ID, tileY + (menuTileHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                              labels_text[i], !selected);
  }

  const auto hints = mappedInput.mapLabels("", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
#endif

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    loadRecentCovers(metrics.homeCoverHeight);
  }
}
