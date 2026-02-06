#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FeatureFlags.h"
#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "SpiBusMutex.h"
#include "activities/TaskShutdown.h"
#include "fontIds.h"
#include "util/StringUtils.h"

void HomeActivity::taskTrampoline(void* param) {
  auto* self = static_cast<HomeActivity*>(param);
  self->displayTaskLoop();
}

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // My Library, TODO, File transfer, Settings
  if (hasContinueReading) count++;
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  if (hasOpdsUrl) count++;
#endif
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!SdMan.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!SdMan.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, "Loading...");
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          updateRequired = true;
        } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                   StringUtils::checkFileExtension(book.path, ".xtc")) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, "Loading...");
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            updateRequired = true;
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  exitTaskRequested.store(false);
  taskHasExited.store(false);

  // Check if we have a book to continue reading
  hasContinueReading = !APP_STATE.openEpubPath.empty() && SdMan.exists(APP_STATE.openEpubPath.c_str());

#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;
#else
  hasOpdsUrl = false;
#endif

  selectorIndex = 0;

  auto metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&HomeActivity::taskTrampoline, "HomeActivityTask",
              8192,               // Stack size
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
  const bool prevPressed = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool nextPressed = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Right);

  const int menuCount = getMenuItemCount();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Calculate dynamic indices based on which options are available
    int idx = 0;
    int menuSelectedIndex = selectorIndex - static_cast<int>(recentBooks.size());
    const int myLibraryIdx = idx++;
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
    const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
#endif
    const int todoIdx = idx++;
    const int fileTransferIdx = idx++;
    const int settingsIdx = idx;

    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
    } else if (menuSelectedIndex == myLibraryIdx) {
      onMyLibraryOpen();
#if ENABLE_INTEGRATIONS && ENABLE_CALIBRE_SYNC
    } else if (selectorIndex == opdsLibraryIdx) {
      onOpdsBrowserOpen();
#endif
    } else if (selectorIndex == todoIdx) {
      onTodoOpen();
    } else if (selectorIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (menuSelectedIndex == settingsIdx) {
      onSettingsOpen();
    }
  } else if (prevPressed) {
    selectorIndex = (selectorIndex + menuCount - 1) % menuCount;
    updateRequired = true;
  } else if (nextPressed) {
    selectorIndex = (selectorIndex + 1) % menuCount;
    updateRequired = true;
  }
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
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Draw book card regardless, fill with message based on `hasContinueReading`
  {
    // Draw cover image as background if available (inside the box)
    // Only load from SD on first render, then use stored buffer
    if (hasContinueReading && hasCoverImage && !coverBmpPath.empty() && !coverRendered) {
      // First time: load cover from SD and render
      SpiBusMutex::Guard guard;
      FsFile file;
      if (SdMan.openFileForRead("HOME", coverBmpPath, file)) {
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
    menuItems.insert(menuItems.begin() + 2, "OPDS Browser");
  }
#endif

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing * 2 +
                         metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()), selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); }, nullptr);

  const auto labels = mappedInput.mapLabels("", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    updateRequired = true;
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}
