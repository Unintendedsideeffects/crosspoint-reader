#include "CalibreSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEMS = 3;
const char* menuNames[MENU_ITEMS] = {"OPDS Server URL", "Username", "Password"};
}  // namespace

void CalibreSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  requestUpdate();
}

void CalibreSettingsActivity::onExit() { Activity::onExit(); }

void CalibreSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  }
}

void CalibreSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // OPDS Server URL
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CALIBRE_WEB_URL),
                                                                   SETTINGS.opdsServerUrl, 127, false),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               strncpy(SETTINGS.opdsServerUrl, kb.text.c_str(), sizeof(SETTINGS.opdsServerUrl) - 1);
                               SETTINGS.opdsServerUrl[sizeof(SETTINGS.opdsServerUrl) - 1] = '\0';
                               SETTINGS.saveToFile();
                             }
                           });
  } else if (selectedIndex == 1) {
    // Username
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_USERNAME),
                                                                   SETTINGS.opdsUsername, 63, false),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               strncpy(SETTINGS.opdsUsername, kb.text.c_str(), sizeof(SETTINGS.opdsUsername) - 1);
                               SETTINGS.opdsUsername[sizeof(SETTINGS.opdsUsername) - 1] = '\0';
                               SETTINGS.saveToFile();
                             }
                           });
  } else if (selectedIndex == 2) {
    // Password
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD),
                                                                   SETTINGS.opdsPassword, 63, false),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               strncpy(SETTINGS.opdsPassword, kb.text.c_str(), sizeof(SETTINGS.opdsPassword) - 1);
                               SETTINGS.opdsPassword[sizeof(SETTINGS.opdsPassword) - 1] = '\0';
                               SETTINGS.saveToFile();
                             }
                           });
  }
}

void CalibreSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  // Draw header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "OPDS Browser", true, EpdFontFamily::BOLD);

  // Draw info text about Calibre
  renderer.drawCenteredText(UI_10_FONT_ID, 40, "For Calibre, add /opds to your URL");

  // Draw selection highlight
  renderer.fillRect(0, 70 + selectedIndex * 30 - 2, pageWidth - 1, 30);

  // Draw menu items
  for (int i = 0; i < MENU_ITEMS; i++) {
    const int settingY = 70 + i * 30;
    const bool isSelected = (i == selectedIndex);

    renderer.drawText(UI_10_FONT_ID, 20, settingY, menuNames[i], !isSelected);

    // Draw status for each setting
    const char* status = "[Not Set]";
    if (i == 0) {
      status = (strlen(SETTINGS.opdsServerUrl) > 0) ? "[Set]" : "[Not Set]";
    } else if (i == 1) {
      status = (strlen(SETTINGS.opdsUsername) > 0) ? "[Set]" : "[Not Set]";
    } else if (i == 2) {
      status = (strlen(SETTINGS.opdsPassword) > 0) ? "[Set]" : "[Not Set]";
    }
    const auto width = renderer.getTextWidth(UI_10_FONT_ID, status);
    renderer.drawText(UI_10_FONT_ID, pageWidth - 20 - width, settingY, status, !isSelected);
  }

  // Draw button hints
  const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
