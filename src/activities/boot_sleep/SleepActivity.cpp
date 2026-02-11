#include "SleepActivity.h"

#include <Epub.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FeatureFlags.h"
#include "SleepExtensionHooks.h"
#include "SpiBusMutex.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "util/StringUtils.h"

namespace {

// Supported image extensions for sleep images
#if ENABLE_IMAGE_SLEEP
const char* SLEEP_IMAGE_EXTENSIONS[] = {".bmp", ".png", ".jpg", ".jpeg"};
#else
const char* SLEEP_IMAGE_EXTENSIONS[] = {".bmp"};  // BMP only when PNG/JPEG disabled
#endif
constexpr int NUM_SLEEP_IMAGE_EXTENSIONS = sizeof(SLEEP_IMAGE_EXTENSIONS) / sizeof(SLEEP_IMAGE_EXTENSIONS[0]);

bool isSupportedSleepImage(const std::string& filename) {
  if (filename.length() < 4) return false;
  std::string lowerFilename = filename;
  std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (int i = 0; i < NUM_SLEEP_IMAGE_EXTENSIONS; i++) {
    size_t extLen = strlen(SLEEP_IMAGE_EXTENSIONS[i]);
    if (lowerFilename.length() >= extLen &&
        lowerFilename.substr(lowerFilename.length() - extLen) == SLEEP_IMAGE_EXTENSIONS[i]) {
      return true;
    }
  }
  return false;
}

bool isBmpFile(const std::string& filename) {
  if (filename.length() < 4) return false;
  std::string lowerFilename = filename;
  std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowerFilename.substr(lowerFilename.length() - 4) == ".bmp";
}

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

struct SleepImageCache {
  bool scanned = false;
  bool sleepDirFound = false;
  std::vector<std::string> validFiles;
};

SleepImageCache sleepImageCache;

bool tryRenderExternalSleepApp(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  const bool rendered = SleepExtensionHooks::renderExternalSleepScreen(renderer, mappedInput);
  if (rendered) {
    Serial.printf("[%lu] [SLP] External sleep app rendered screen\n", millis());
  }
  return rendered;
}

void validateSleepImagesOnce() {
  SleepCacheMutex::Guard guard;
  if (sleepImageCache.scanned) {
    return;
  }

  // Clear before scanning (in case of previous partial scan)
  sleepImageCache.validFiles.clear();

  auto dir = Storage.open("/sleep");
  if (!(dir && dir.isDirectory())) {
    if (dir) dir.close();
    sleepImageCache.scanned = true;
    sleepImageCache.sleepDirFound = false;
    return;
  }

  sleepImageCache.scanned = true;
  sleepImageCache.sleepDirFound = true;
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

    if (isSupportedSleepImage(filename)) {
      if (isBmpFile(filename)) {
        // Validate BMP files by parsing headers
        Bitmap bitmap(file, true);
        const auto err = bitmap.parseHeaders();
        if (err == BmpReaderError::Ok) {
          sleepImageCache.validFiles.emplace_back(filename);
        } else {
          Serial.printf("[SLP] Invalid BMP in /sleep: %s (%s)\n", filename.c_str(), Bitmap::errorToString(err));
        }
      } else {
        // For PNG/JPEG, validate by checking if decoder can get dimensions
        std::string fullPath = "/sleep/" + filename;
        const ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(fullPath);
        if (decoder) {
          ImageDimensions dims = {0, 0};
          if (decoder->getDimensions(fullPath, dims) && dims.width > 0 && dims.height > 0) {
            sleepImageCache.validFiles.emplace_back(filename);
            Serial.printf("[SLP] Valid %s in /sleep: %s (%dx%d)\n", decoder->getFormatName(), filename.c_str(),
                          dims.width, dims.height);
          } else {
            Serial.printf("[SLP] Invalid image in /sleep: %s (could not read dimensions)\n", filename.c_str());
          }
        }
      }
    }
    file.close();
  }
  dir.close();

  Serial.printf("[%lu] [SLP] Found %d valid sleep images\n", millis(), sleepImageCache.validFiles.size());
}
}  // namespace

void invalidateSleepImageCache() {
  SleepCacheMutex::Guard guard;
  sleepImageCache.scanned = false;
  sleepImageCache.sleepDirFound = false;
  sleepImageCache.validFiles.clear();
  Serial.printf("[%lu] [SLP] Sleep image cache invalidated\n", millis());
}

void SleepActivity::onEnter() {
  Activity::onEnter();
  // Skip the "Entering Sleep..." popup to avoid unnecessary screen refresh
  // The sleep screen will be displayed immediately anyway

  // Initialize image decoder factory
  ImageDecoderFactory::initialize();

  // Optional extension point for third-party sleep apps.
  if (tryRenderExternalSleepApp(renderer, mappedInput)) {
    return;
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      return renderCoverSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  SpiBusMutex::Guard guard;
  SleepCacheMutex::Guard cacheGuard;
  validateSleepImagesOnce();
  const auto numFiles = sleepImageCache.validFiles.size();
  if (sleepImageCache.sleepDirFound && numFiles > 0) {
    // Generate a random number between 0 and numFiles-1
    auto randomFileIndex = random(numFiles);
    // If we picked the same image as last time, pick the next one
    if (numFiles > 1 && randomFileIndex == APP_STATE.lastSleepImage) {
      randomFileIndex = (randomFileIndex + 1) % numFiles;
    }
    // Only save to file if the selection actually changed
    const bool selectionChanged = (APP_STATE.lastSleepImage != randomFileIndex);
    APP_STATE.lastSleepImage = randomFileIndex;
    if (selectionChanged) {
      APP_STATE.saveToFile();
    }
    const auto filename = "/sleep/" + sleepImageCache.validFiles[randomFileIndex];
    Serial.printf("[%lu] [SLP] Loading: %s\n", millis(), filename.c_str());

    if (isBmpFile(filename)) {
      // Use existing BMP rendering path
      FsFile file;
      if (Storage.openFileForRead("SLP", filename, file)) {
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          return;
        }
        Serial.printf("[%lu] [SLP] Invalid BMP: %s\n", millis(), filename.c_str());
      }
    } else {
      // Use new PNG/JPEG rendering path
      renderImageSleepScreen(filename);
      return;
    }
  }

  // Look for sleep image on the root of the sd card
  // Check multiple formats in order of preference
  const char* rootSleepImages[] = {"/sleep.bmp", "/sleep.png", "/sleep.jpg", "/sleep.jpeg"};
  for (const char* sleepImagePath : rootSleepImages) {
    if (isBmpFile(sleepImagePath)) {
      FsFile file;
      if (Storage.openFileForRead("SLP", sleepImagePath, file)) {
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          Serial.printf("[%lu] [SLP] Loading: %s\n", millis(), sleepImagePath);
          renderBitmapSleepScreen(bitmap);
          return;
        }
      }
    } else {
      // Check if PNG/JPEG file exists and is valid
      const ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(sleepImagePath);
      if (decoder) {
        ImageDimensions dims = {0, 0};
        if (decoder->getDimensions(sleepImagePath, dims) && dims.width > 0 && dims.height > 0) {
          Serial.printf("[%lu] [SLP] Loading: %s\n", millis(), sleepImagePath);
          renderImageSleepScreen(sleepImagePath);
          return;
        }
      }
    }
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
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

void SleepActivity::renderImageSleepScreen(const std::string& imagePath) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(imagePath);
  if (!decoder) {
    Serial.printf("[%lu] [SLP] No decoder for: %s\n", millis(), imagePath.c_str());
    return renderDefaultSleepScreen();
  }

  ImageDimensions dims = {0, 0};
  if (!decoder->getDimensions(imagePath, dims) || dims.width <= 0 || dims.height <= 0) {
    Serial.printf("[%lu] [SLP] Could not get dimensions for: %s\n", millis(), imagePath.c_str());
    return renderDefaultSleepScreen();
  }

  Serial.printf("[%lu] [SLP] Image %dx%d, screen %dx%d\n", millis(), dims.width, dims.height, pageWidth, pageHeight);

  // Calculate scale and position
  float scaleX = (dims.width > pageWidth) ? static_cast<float>(pageWidth) / dims.width : 1.0f;
  float scaleY = (dims.height > pageHeight) ? static_cast<float>(pageHeight) / dims.height : 1.0f;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;
  if (scale > 1.0f) scale = 1.0f;

  int displayWidth = static_cast<int>(dims.width * scale);
  int displayHeight = static_cast<int>(dims.height * scale);

  // Center the image
  int x = (pageWidth - displayWidth) / 2;
  int y = (pageHeight - displayHeight) / 2;

  Serial.printf("[%lu] [SLP] Rendering at %d,%d size %dx%d (scale %.2f)\n", millis(), x, y, displayWidth, displayHeight,
                scale);

  // Clear screen and prepare for rendering
  renderer.clearScreen();

  // Check if grayscale is enabled (no filter selected)
  const bool useGrayscale = SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  // Configure render settings
  RenderConfig config;
  config.x = x;
  config.y = y;
  config.maxWidth = pageWidth;
  config.maxHeight = pageHeight;
  config.useGrayscale = useGrayscale;
  config.useDithering = true;

  // Render the image to framebuffer (BW pass)
  renderer.setRenderMode(GfxRenderer::BW);
  if (!decoder->decodeToFramebuffer(imagePath, renderer, config)) {
    Serial.printf("[%lu] [SLP] Failed to decode: %s\n", millis(), imagePath.c_str());
    return renderDefaultSleepScreen();
  }

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  // If grayscale is enabled, do additional passes for 4-level grayscale
  if (useGrayscale) {
    // LSB pass
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    decoder->decodeToFramebuffer(imagePath, renderer, config);
    renderer.copyGrayscaleLsbBuffers();

    // MSB pass
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    decoder->decodeToFramebuffer(imagePath, renderer, config);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  SpiBusMutex::Guard guard;
  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
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
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      Serial.println("[SLP] Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".txt")) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      Serial.println("[SLP] Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      Serial.println("[SLP] No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".epub")) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      Serial.println("[SLP] Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      Serial.println("[SLP] Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  FsFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      Serial.printf("[SLP] Rendering sleep cover: %s\n", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
