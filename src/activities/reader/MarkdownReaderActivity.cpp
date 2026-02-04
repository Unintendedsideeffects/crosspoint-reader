#include "MarkdownReaderActivity.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <MarkdownRenderer.h>
#include <SDCardManager.h>
#include <esp_task_wdt.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "ScreenComponents.h"
#include "SpiBusMutex.h"
#include "TocActivity.h"
#include "activities/TaskShutdown.h"
#include "fontIds.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
constexpr int statusBarMargin = 19;
constexpr int progressBarMarginTop = 1;
}  // namespace

bool MarkdownReaderActivity::waitForRenderingMutex() {
  int retries = 0;
  while (xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    if (++retries >= 50 || taskShouldExit.load()) {
      Serial.printf("[%lu] [MDR] Mutex timeout after %d retries\n", millis(), retries);
      return false;
    }
    esp_task_wdt_reset();
  }
  return true;
}

void MarkdownReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<MarkdownReaderActivity*>(param);
  self->displayTaskLoop();
}

void MarkdownReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!markdown) {
    return;
  }

  switch (SETTINGS.orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }

  renderingMutex = xSemaphoreCreateMutex();
  taskShouldExit.store(false);
  taskHasExited.store(false);

  markdown->setupCacheDir();
  htmlReady.store(markdown->ensureHtml());

  // Parse AST for navigation features (non-blocking, best effort)
  astReady.store(markdown->parseToAst());

  APP_STATE.openEpubPath = markdown->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(markdown->getPath(), markdown->getTitle(), "");

  loadProgress();
  updateRequired = true;

  xTaskCreate(&MarkdownReaderActivity::taskTrampoline, "MarkdownReaderActivityTask", 8192, this, 1, &displayTaskHandle);
}

void MarkdownReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Signal task to exit gracefully
  if (displayTaskHandle != nullptr) {
    TaskShutdown::requestExit(taskShouldExit, taskHasExited, displayTaskHandle);
  } else {
    taskShouldExit.store(true);
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
  section.reset();
  markdown.reset();
}

void MarkdownReaderActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoBack();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showTableOfContents();
    return;
  }

  // Long press for heading navigation (when enabled and AST is available)
  if (SETTINGS.longPressChapterSkip && astReady.load()) {
    constexpr unsigned long headingSkipMs = 500;
    const bool leftHeld = mappedInput.isPressed(MappedInputManager::Button::Left) ||
                          mappedInput.isPressed(MappedInputManager::Button::PageBack);
    const bool rightHeld = mappedInput.isPressed(MappedInputManager::Button::Right) ||
                           mappedInput.isPressed(MappedInputManager::Button::PageForward);

    if (leftHeld && mappedInput.getHeldTime() >= headingSkipMs) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
          mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
        jumpToPrevHeading();
        return;
      }
    }

    if (rightHeld && mappedInput.getHeldTime() >= headingSkipMs) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
        jumpToNextHeading();
        return;
      }
    }
  }

  const bool usePressForPageTurn = !SETTINGS.longPressChapterSkip;
  const bool prevTriggered = usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasReleased(MappedInputManager::Button::Left));
  const bool powerPageTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered = usePressForPageTurn
                                 ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasPressed(MappedInputManager::Button::Right))
                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasReleased(MappedInputManager::Button::Right));

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (!section || section->pageCount == 0) {
    updateRequired = true;
    return;
  }

  if (prevTriggered && section->currentPage > 0) {
    section->currentPage--;
    updateRequired = true;
  } else if (nextTriggered && section->currentPage < section->pageCount - 1) {
    section->currentPage++;
    updateRequired = true;
  }
}

void MarkdownReaderActivity::displayTaskLoop() {
  while (!taskShouldExit.load()) {
    if (updateRequired) {
      updateRequired = false;
      if (!waitForRenderingMutex()) {
        if (taskShouldExit.load()) {
          break;
        }
        continue;
      }
      renderScreen();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  taskHasExited.store(true);
  vTaskDelete(nullptr);  // Self-delete when signaled to exit
}

void MarkdownReaderActivity::renderScreen() {
  if (!markdown) {
    return;
  }

  if (!htmlReady.load()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "Markdown error", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;
  orientedMarginBottom += SETTINGS.screenMargin;

  if (SETTINGS.statusBar != CrossPointSettings::STATUS_BAR_MODE::NONE) {
    const bool showProgressBar = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR ||
                                 SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::ONLY_PROGRESS_BAR;
    orientedMarginBottom += statusBarMargin - SETTINGS.screenMargin +
                            (showProgressBar ? (ScreenComponents::BOOK_PROGRESS_BAR_HEIGHT + progressBarMarginTop) : 0);
  }

  if (!section) {
    const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

    section.reset(
        new HtmlSection(markdown->getHtmlPath(), markdown->getCachePath(), markdown->getContentBasePath(), renderer));

    bool sectionLoaded = false;
    {
      SpiBusMutex::Guard guard;
      sectionLoaded = section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                               SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment,
                                               viewportWidth, viewportHeight, SETTINGS.hyphenationEnabled,
                                               static_cast<uint32_t>(markdown->getFileSize()));
    }

    if (!sectionLoaded) {
      constexpr int barWidth = 200;
      constexpr int barHeight = 10;
      constexpr int boxMargin = 20;
      const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, "Indexing...");
      const int boxWidthWithBar = (barWidth > textWidth ? barWidth : textWidth) + boxMargin * 2;
      const int boxWidthNoBar = textWidth + boxMargin * 2;
      const int boxHeightWithBar = renderer.getLineHeight(UI_12_FONT_ID) + barHeight + boxMargin * 3;
      const int boxHeightNoBar = renderer.getLineHeight(UI_12_FONT_ID) + boxMargin * 2;
      const int boxXWithBar = (renderer.getScreenWidth() - boxWidthWithBar) / 2;
      const int boxXNoBar = (renderer.getScreenWidth() - boxWidthNoBar) / 2;
      constexpr int boxY = 50;
      const int barX = boxXWithBar + (boxWidthWithBar - barWidth) / 2;
      const int barY = boxY + renderer.getLineHeight(UI_12_FONT_ID) + boxMargin * 2;

      renderer.fillRect(boxXNoBar, boxY, boxWidthNoBar, boxHeightNoBar, false);
      renderer.drawText(UI_12_FONT_ID, boxXNoBar + boxMargin, boxY + boxMargin, "Indexing...");
      renderer.drawRect(boxXNoBar + 5, boxY + 5, boxWidthNoBar - 10, boxHeightNoBar - 10);
      renderer.displayBuffer();
      pagesUntilFullRefresh = 0;

      auto progressSetup = [this, boxXWithBar, boxWidthWithBar, boxHeightWithBar, boxY, barX, barY, barWidth,
                            barHeight] {
        renderer.fillRect(boxXWithBar, boxY, boxWidthWithBar, boxHeightWithBar, false);
        renderer.drawText(UI_12_FONT_ID, boxXWithBar + boxMargin, boxY + boxMargin, "Indexing...");
        renderer.drawRect(boxXWithBar + 5, boxY + 5, boxWidthWithBar - 10, boxHeightWithBar - 10);
        renderer.drawRect(barX, barY, barWidth, barHeight);
        renderer.displayBuffer();
      };

      auto progressCallback = [this, barX, barY, barWidth, barHeight](int progress) {
        const int fillWidth = (barWidth - 2) * progress / 100;
        renderer.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, true);
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      };

      if (!section->createSectionFile(
              SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
              SETTINGS.paragraphAlignment, viewportWidth, viewportHeight, SETTINGS.hyphenationEnabled,
              static_cast<uint32_t>(markdown->getFileSize()), progressSetup, progressCallback)) {
        Serial.printf("[%lu] [MDR] Failed to build markdown cache\n", millis());
        section.reset();
        return;
      }
    }

    if (hasSavedPage) {
      if (section->pageCount > 0) {
        section->currentPage = std::min(savedPage, static_cast<int>(section->pageCount - 1));
      } else {
        section->currentPage = 0;
      }
      hasSavedPage = false;
    }

    if (astReady.load() && markdown->getAst()) {
      auto* nav = markdown->getNavigation();
      if (nav) {
        MarkdownRenderer mdRenderer(renderer, SETTINGS.getReaderFontId(), viewportWidth, viewportHeight,
                                    SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
                                    SETTINGS.paragraphAlignment, SETTINGS.hyphenationEnabled,
                                    markdown->getContentBasePath());
        // Full render is required to compute accurate node->page mapping based on layout.
        mdRenderer.render(*markdown->getAst(), [](std::unique_ptr<Page> page) { (void)page; });
        nav->updatePageNumbers(mdRenderer.getNodeToPageMap());
      }
    }
  }

  renderer.clearScreen();

  if (!section || section->pageCount == 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "Empty document", true, EpdFontFamily::BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "Out of bounds", true, EpdFontFamily::BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  std::unique_ptr<Page> page;
  {
    SpiBusMutex::Guard guard;
    page = section->loadPageFromSectionFile();
  }

  if (!page) {
    section->clearCache();
    section.reset();
    return;
  }

  renderContents(std::move(page), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  saveProgress();
}

void MarkdownReaderActivity::renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                                            int orientedMarginBottom, int orientedMarginLeft) {
  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  renderer.storeBwBuffer();

  if (SETTINGS.textAntiAliasing) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  renderer.restoreBwBuffer();
}

void MarkdownReaderActivity::renderStatusBar(int orientedMarginRight, int orientedMarginBottom,
                                             int orientedMarginLeft) const {
  const bool showProgressPercentage = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showProgressBar = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR ||
                               SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::ONLY_PROGRESS_BAR;
  const bool showProgressText = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL ||
                                SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR;
  const bool showBattery = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR;
  const bool showTitle = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                         SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL ||
                         SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;

  const auto screenHeight = renderer.getScreenHeight();
  const auto textY = screenHeight - orientedMarginBottom - 4;
  int progressTextWidth = 0;

  float progress = 0.0f;
  if (section && section->pageCount > 0) {
    progress = static_cast<float>(section->currentPage + 1) / static_cast<float>(section->pageCount) * 100.0f;
  }

  if (showProgressText || showProgressPercentage) {
    char progressStr[32];
    if (showProgressPercentage) {
      snprintf(progressStr, sizeof(progressStr), "%d/%d  %.0f%%", section ? section->currentPage + 1 : 0,
               section ? section->pageCount : 0, progress);
    } else {
      snprintf(progressStr, sizeof(progressStr), "%d/%d", section ? section->currentPage + 1 : 0,
               section ? section->pageCount : 0);
    }

    progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressStr);
    renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - orientedMarginRight - progressTextWidth, textY,
                      progressStr);
  }

  if (showProgressBar) {
    ScreenComponents::drawBookProgressBar(renderer, static_cast<size_t>(progress));
  }

  if (showBattery) {
    ScreenComponents::drawBattery(renderer, orientedMarginLeft + 1, textY, showBatteryPercentage);
  }

  if (showTitle && markdown) {
    const int titleMarginLeft = 50 + 30 + orientedMarginLeft;
    const int titleMarginRight = progressTextWidth + 30 + orientedMarginRight;
    const int availableTextWidth = renderer.getScreenWidth() - titleMarginLeft - titleMarginRight;

    std::string title = markdown->getTitle();
    int titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    while (titleWidth > availableTextWidth && title.length() > 11) {
      title.replace(title.length() - 8, 8, "...");
      titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    }

    renderer.drawText(SMALL_FONT_ID, titleMarginLeft + (availableTextWidth - titleWidth) / 2, textY, title.c_str());
  }
}

void MarkdownReaderActivity::saveProgress() const {
  if (!markdown || !section) {
    return;
  }
  SpiBusMutex::Guard guard;
  FsFile f;
  if (SdMan.openFileForWrite("MDR", markdown->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = section->currentPage & 0xFF;
    data[1] = (section->currentPage >> 8) & 0xFF;
    data[2] = 0;
    data[3] = 0;
    f.write(data, 4);
    f.close();
  }
}

void MarkdownReaderActivity::loadProgress() {
  if (!markdown) {
    return;
  }

  SpiBusMutex::Guard guard;
  FsFile f;
  if (SdMan.openFileForRead("MDR", markdown->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      savedPage = data[0] + (data[1] << 8);
      hasSavedPage = true;
    }
    f.close();
  }
}

void MarkdownReaderActivity::jumpToNextHeading() {
  if (!astReady.load() || !markdown || !section) {
    return;
  }

  const auto* nav = markdown->getNavigation();
  if (!nav) {
    return;
  }

  auto nextPage = nav->findNextHeading(static_cast<size_t>(section->currentPage));
  if (nextPage.has_value() && nextPage.value() < static_cast<size_t>(section->pageCount)) {
    section->currentPage = static_cast<int>(nextPage.value());
    updateRequired = true;
  }
}

void MarkdownReaderActivity::jumpToPrevHeading() {
  if (!astReady.load() || !markdown || !section) {
    return;
  }

  const auto* nav = markdown->getNavigation();
  if (!nav) {
    return;
  }

  auto prevPage = nav->findPrevHeading(static_cast<size_t>(section->currentPage));
  if (prevPage.has_value()) {
    section->currentPage = static_cast<int>(prevPage.value());
    updateRequired = true;
  }
}

void MarkdownReaderActivity::showTableOfContents() {
  if (!astReady.load() || !markdown || !section) {
    return;
  }

  auto* nav = markdown->getNavigation();
  if (!nav || nav->getToc().empty()) {
    return;
  }

  if (!waitForRenderingMutex()) {
    return;
  }
  exitActivity();
  enterNewActivity(new TocActivity(
      renderer, mappedInput, nav->getToc(),
      [this] {
        exitActivity();
        updateRequired = true;
      },
      [this, nav](size_t tocIndex) {
        if (section) {
          auto page = nav->findHeadingPage(tocIndex);
          if (page.has_value() && page.value() < static_cast<size_t>(section->pageCount)) {
            section->currentPage = static_cast<int>(page.value());
          } else if (section->pageCount > 0) {
            section->currentPage = 0;
          }
        }
        exitActivity();
        updateRequired = true;
      }));
  xSemaphoreGive(renderingMutex);
}
