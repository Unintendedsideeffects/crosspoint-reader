#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "activities/boot_sleep/BootActivity.h"
#include "fontIds.h"

HardwareSerial Serial;
SPIClass SPI;

// Activity constructors use this type by reference. For the host harness we only
// render static snapshots and don't consume input.
class MappedInputManager {};

namespace {

void installFonts(GfxRenderer& renderer) {
  static EpdFont smallFont(&notosans_8_regular);
  static EpdFontFamily smallFontFamily(&smallFont);

  static EpdFont ui10RegularFont(&ubuntu_10_regular);
  static EpdFont ui10BoldFont(&ubuntu_10_bold);
  static EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

  static EpdFont ui12RegularFont(&ubuntu_12_regular);
  static EpdFont ui12BoldFont(&ubuntu_12_bold);
  static EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

  static EpdFont bookerly14RegularFont(&bookerly_14_regular);
  static EpdFont bookerly14BoldFont(&bookerly_14_bold);
  static EpdFont bookerly14ItalicFont(&bookerly_14_italic);
  static EpdFont bookerly14BoldItalicFont(&bookerly_14_bolditalic);
  static EpdFontFamily bookerly14FontFamily(&bookerly14RegularFont, &bookerly14BoldFont, &bookerly14ItalicFont,
                                            &bookerly14BoldItalicFont);

  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(BOOKERLY_14_FONT_ID, bookerly14FontFamily);
}

void saveSnapshot(HalDisplay& display, const std::filesystem::path& outDir, const std::string& name) {
  const auto outputPath = outDir / (name + ".pbm");
  const auto outputPathStr = outputPath.string();
  display.saveFrameBufferAsPBM(outputPathStr.c_str());
  std::cout << "wrote " << outputPathStr << '\n';
}

void drawHeader(GfxRenderer& renderer, const char* title) {
  const int right = renderer.getScreenWidth() - 18;
  renderer.drawText(UI_12_FONT_ID, 18, 14, title, true, EpdFontFamily::BOLD);
  renderer.drawLine(18, 40, right, 40);
}

void drawBoot(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  BootActivity boot(renderer, mappedInput);
  boot.onEnter();
}

void drawHomeMock(GfxRenderer& renderer) {
  renderer.clearScreen();
  drawHeader(renderer, "Home");

  renderer.drawRoundedRect(18, 56, 444, 184, 2, 8, true);
  renderer.drawText(UI_10_FONT_ID, 30, 70, "Continue reading", true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, 30, 95, "The Left Hand of Darkness", true);
  renderer.drawText(SMALL_FONT_ID, 30, 116, "by Ursula K. Le Guin", true);
  renderer.drawRect(354, 72, 90, 144, 1, true);
  renderer.drawCenteredText(SMALL_FONT_ID, 228, "42% complete", true);

  renderer.drawRoundedRect(18, 254, 444, 44, 2, 8, true);
  renderer.drawText(UI_10_FONT_ID, 30, 270, "My Library", true, EpdFontFamily::BOLD);

  renderer.drawRoundedRect(18, 306, 444, 44, 1, 8, true);
  renderer.drawText(UI_10_FONT_ID, 30, 322, "File Transfer", true);

  renderer.drawRoundedRect(18, 358, 444, 44, 1, 8, true);
  renderer.drawText(UI_10_FONT_ID, 30, 374, "Settings", true);

  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Open", "Up", "Down");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void drawSettingsMock(GfxRenderer& renderer) {
  renderer.clearScreen();
  drawHeader(renderer, "Settings");

  renderer.drawText(SMALL_FONT_ID, 24, 60, "Display", true, EpdFontFamily::BOLD);
  renderer.drawRoundedRect(24, 78, 432, 42, 1, 6, true);
  renderer.drawText(UI_10_FONT_ID, 34, 92, "UI Theme", true);
  renderer.drawText(UI_10_FONT_ID, 356, 92, "Lyra", true, EpdFontFamily::BOLD);

  renderer.drawRoundedRect(24, 126, 432, 42, 1, 6, true);
  renderer.drawText(UI_10_FONT_ID, 34, 140, "Refresh Frequency", true);
  renderer.drawText(UI_10_FONT_ID, 332, 140, "5 pages", true, EpdFontFamily::BOLD);

  renderer.drawText(SMALL_FONT_ID, 24, 190, "Buttons", true, EpdFontFamily::BOLD);
  renderer.drawRoundedRect(24, 208, 432, 42, 1, 6, true);
  renderer.drawText(UI_10_FONT_ID, 34, 222, "Select on Power", true);
  renderer.drawText(UI_10_FONT_ID, 360, 222, "On", true, EpdFontFamily::BOLD);

  renderer.drawRoundedRect(24, 256, 432, 42, 1, 6, true);
  renderer.drawText(UI_10_FONT_ID, 34, 270, "Dual Side Layout", true);
  renderer.drawText(UI_10_FONT_ID, 330, 270, "Forced", true, EpdFontFamily::BOLD);

  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Edit", "Prev", "Next");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void drawFactoryResetMock(GfxRenderer& renderer) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Factory Reset", true, EpdFontFamily::BOLD);

  const int centerY = renderer.getScreenHeight() / 2;
  renderer.drawCenteredText(UI_10_FONT_ID, centerY - 90, "This will erase all CrossPoint data.", true);
  renderer.drawCenteredText(UI_10_FONT_ID, centerY - 55, "Settings, WiFi, reading state,", true);
  renderer.drawCenteredText(UI_10_FONT_ID, centerY - 25, "sync credentials, and cache", true);
  renderer.drawCenteredText(UI_10_FONT_ID, centerY + 5, "will be reset.", true);
  renderer.drawCenteredText(UI_10_FONT_ID, centerY + 45, "Books on the SD card are kept.", true, EpdFontFamily::BOLD);

  renderer.drawButtonHints(UI_10_FONT_ID, "Cancel", "Reset", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void drawReaderMock(GfxRenderer& renderer) {
  renderer.clearScreen();
  drawHeader(renderer, "Reader");

  renderer.drawText(BOOKERLY_14_FONT_ID, 28, 62, "We walked through the quiet city as", true);
  renderer.drawText(BOOKERLY_14_FONT_ID, 28, 88, "the daylight thinned into a pale", true);
  renderer.drawText(BOOKERLY_14_FONT_ID, 28, 114, "silver dusk over the harbor.", true);
  renderer.drawText(BOOKERLY_14_FONT_ID, 28, 140, "", true);
  renderer.drawText(BOOKERLY_14_FONT_ID, 28, 166, "No one spoke. The only sound was", true);
  renderer.drawText(BOOKERLY_14_FONT_ID, 28, 192, "the wind pressing at the shutters", true);
  renderer.drawText(BOOKERLY_14_FONT_ID, 28, 218, "and the pages turning in my hand.", true);

  renderer.drawRect(28, 760, 424, 10, 1, true);
  renderer.fillRect(30, 762, 242, 6, true);
  renderer.drawText(SMALL_FONT_ID, 30, 736, "Chapter 8", true);
  renderer.drawText(SMALL_FONT_ID, 394, 736, "57%", true);

  renderer.drawButtonHints(UI_10_FONT_ID, "Menu", "Select", "Prev", "Next");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

}  // namespace

int main(int argc, char* argv[]) {
  const std::filesystem::path outputDir = (argc > 1) ? argv[1] : "build/screen-previews";
  std::filesystem::create_directories(outputDir);

  HalDisplay display;
  display.begin();

  GfxRenderer renderer(display);
  renderer.begin();
  installFonts(renderer);

  MappedInputManager mappedInput;

  const std::vector<std::pair<std::string, std::function<void()>>> scenarios = {
      {"01_boot", [&] { drawBoot(renderer, mappedInput); }},
      {"02_home_mock", [&] { drawHomeMock(renderer); }},
      {"03_settings_mock", [&] { drawSettingsMock(renderer); }},
      {"04_factory_reset_mock", [&] { drawFactoryResetMock(renderer); }},
      {"05_reader_mock", [&] { drawReaderMock(renderer); }},
  };

  for (const auto& [name, render] : scenarios) {
    render();
    saveSnapshot(display, outputDir, name);
  }

  return 0;
}
