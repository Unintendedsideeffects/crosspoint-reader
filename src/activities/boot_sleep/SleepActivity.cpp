#include "SleepActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Txt.h>
#include <Xtc.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SpiBusMutex.h"
#include "fontIds.h"
#include "images/CrossLarge.h"
#include "util/StringUtils.h"

namespace {
namespace SleepCacheMutex {
StaticSemaphore_t mutexBuffer;
SemaphoreHandle_t get() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutexStatic(&mutexBuffer);
  return mutex;
}
void lock() {
  auto mutex = get();
  if (mutex) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  }
}
void unlock() {
  auto mutex = get();
  if (mutex) {
    xSemaphoreGiveRecursive(mutex);
  }
}
struct Guard {
  Guard() { lock(); }
  ~Guard() { unlock(); }
};
}  // namespace SleepCacheMutex

struct SleepBmpCache {
  bool scanned = false;
  bool sleepDirFound = false;
  std::vector<std::string> validFiles;
};

SleepBmpCache sleepBmpCache;

void validateSleepBmpsOnce() {
  SleepCacheMutex::Guard guard;
  if (sleepBmpCache.scanned) {
    return;
  }

  sleepBmpCache.validFiles.clear();

  auto dir = SdMan.open("/sleep");
  if (!(dir && dir.isDirectory())) {
    if (dir) dir.close();
    sleepBmpCache.scanned = true;
    sleepBmpCache.sleepDirFound = false;
    return;
  }

  sleepBmpCache.scanned = true;
  sleepBmpCache.sleepDirFound = true;
  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }
    file.getName(name, sizeof(name));
    auto filename = std::string(name);
    if (filename.empty() || filename[0] == '.') {
      file.close();
      continue;
    }

    // StringUtils::checkFileExtension is case-insensitive.
    if (!StringUtils::checkFileExtension(filename, ".bmp")) {
      Serial.printf("[%lu] [SLP] Skipping non-.bmp file name: %s\n", millis(), name);
      file.close();
      continue;
    }
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      Serial.printf("[%lu] [SLP] Skipping invalid BMP file: %s\n", millis(), name);
      file.close();
      continue;
    }
    sleepBmpCache.validFiles.emplace_back(filename);
    file.close();
  }
  dir.close();
}
}  // namespace

void invalidateSleepBmpCache() {
  SleepCacheMutex::Guard guard;
  sleepBmpCache.scanned = false;
  sleepBmpCache.sleepDirFound = false;
  sleepBmpCache.validFiles.clear();
  Serial.printf("[%lu] [SLP] Sleep BMP cache invalidated\n", millis());
}

void SleepActivity::onEnter() {
  Activity::onEnter();
  // Skip the "Entering Sleep..." popup to avoid unnecessary screen refresh
  // The sleep screen will be displayed immediately anyway

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::BLANK) {
    return renderBlankSleepScreen();
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    return renderCustomSleepScreen();
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER) {
    return renderCoverSleepScreen();
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderCustomSleepScreen() const {
  SpiBusMutex::Guard guard;
  SleepCacheMutex::Guard cacheGuard;
  validateSleepBmpsOnce();
  const auto numFiles = sleepBmpCache.validFiles.size();
  if (sleepBmpCache.sleepDirFound && numFiles > 0) {
    // Generate a random number between 0 and numFiles-1
    auto randomFileIndex = random(numFiles);
    // If we picked the same image as last time, pick the next one.
    if (numFiles > 1 && randomFileIndex == APP_STATE.lastSleepImage) {
      randomFileIndex = (randomFileIndex + 1) % numFiles;
    }
    // Only save to file if the selection actually changed
    const bool selectionChanged = (APP_STATE.lastSleepImage != randomFileIndex);
    APP_STATE.lastSleepImage = randomFileIndex;
    if (selectionChanged) {
      APP_STATE.saveToFile();
    }
    const auto filename = "/sleep/" + sleepBmpCache.validFiles[randomFileIndex];
    FsFile file;
    if (SdMan.openFileForRead("SLP", filename, file)) {
      Serial.printf("[%lu] [SLP] Randomly loading: /sleep/%s\n", millis(),
                    sleepBmpCache.validFiles[randomFileIndex].c_str());
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        return;
      }
    }
  }

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  FsFile file;
  if (SdMan.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      Serial.printf("[%lu] [SLP] Loading: /sleep.bmp\n", millis());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(CrossLarge, (pageWidth - 128) / 2, (pageHeight - 128) / 2, 128, 128);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, "CrossPoint", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, "SLEEPING");

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  Serial.printf("[%lu] [SLP] bitmap %d x %d, screen %d x %d\n", millis(), bitmap.getWidth(), bitmap.getHeight(),
                pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    Serial.printf("[%lu] [SLP] bitmap ratio: %f, screen ratio: %f\n", millis(), ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        Serial.printf("[%lu] [SLP] Cropping bitmap x: %f\n", millis(), cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      Serial.printf("[%lu] [SLP] Centering with ratio %f to y=%d\n", millis(), ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        Serial.printf("[%lu] [SLP] Cropping bitmap y: %f\n", millis(), cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      Serial.printf("[%lu] [SLP] Centering with ratio %f to x=%d\n", millis(), ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  Serial.printf("[%lu] [SLP] drawing to %d x %d\n", millis(), x, y);
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  SpiBusMutex::Guard guard;
  if (APP_STATE.openEpubPath.empty()) {
    return renderDefaultSleepScreen();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".xtc") ||
      StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".xtch")) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      Serial.println("[SLP] Failed to load last XTC");
      return renderDefaultSleepScreen();
    }

    if (!lastXtc.generateCoverBmp()) {
      Serial.println("[SLP] Failed to generate XTC cover bmp");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".txt")) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      Serial.println("[SLP] Failed to load last TXT");
      return renderDefaultSleepScreen();
    }

    if (!lastTxt.generateCoverBmp()) {
      Serial.println("[SLP] No cover image found for TXT file");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".epub")) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastEpub.load()) {
      Serial.println("[SLP] Failed to load last epub");
      return renderDefaultSleepScreen();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      Serial.println("[SLP] Failed to generate cover bmp");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return renderDefaultSleepScreen();
  }

  FsFile file;
  if (SdMan.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      Serial.printf("[SLP] Rendering sleep cover: %s\n", coverBmpPath);
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
