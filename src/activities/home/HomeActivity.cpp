#include "HomeActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
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

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
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
        } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                   StringUtils::checkFileExtension(book.path, ".xtc")) {
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
        }
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
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

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

void HomeActivity::render(Activity::RenderLock&&) {
  auto metrics = UITheme::getInstance().getMetrics();
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
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  if (hasOpdsUrl) {
    // Insert OPDS Browser after My Library
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
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

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}
