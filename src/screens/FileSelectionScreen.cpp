#include "FileSelectionScreen.h"

#include <EpdRenderer.h>
#include <SD.h>

void FileSelectionScreen::taskTrampoline(void* param) {
  auto* self = static_cast<FileSelectionScreen*>(param);
  self->displayTaskLoop();
}

void FileSelectionScreen::onEnter() {
  files.clear();
  auto root = SD.open("/");
  File file;
  while ((file = root.openNextFile())) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    auto filename = std::string(file.name());
    if (filename.substr(filename.length() - 5) != ".epub" || filename[0] == '.') {
      file.close();
      continue;
    }

    files.emplace_back(filename);
    file.close();
  }
  root.close();

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&FileSelectionScreen::taskTrampoline, "FileSelectionScreenTask",
              1024,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void FileSelectionScreen::onExit() {
  vTaskDelete(displayTaskHandle);
  displayTaskHandle = nullptr;
}

void FileSelectionScreen::handleInput(const Input input) {
  if (input.button == VOLUME_DOWN) {
    selectorIndex = (selectorIndex + 1) % files.size();
    updateRequired = true;
  } else if (input.button == VOLUME_UP) {
    selectorIndex = (selectorIndex + files.size() - 1) % files.size();
    updateRequired = true;
  } else if (input.button == CONFIRM) {
    Serial.printf("Selected file: %s\n", files[selectorIndex].c_str());
    onSelect("/" + files[selectorIndex]);
  }
}

void FileSelectionScreen::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      render();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void FileSelectionScreen::render() const {
  renderer->clearScreen();

  const auto pageWidth = renderer->getPageWidth();
  const auto titleWidth = renderer->getTextWidth("CrossPoint Reader", true);
  renderer->drawText((pageWidth - titleWidth) / 2, 0, "CrossPoint Reader", true);

  // Draw selection
  renderer->fillRect(0, 50 + selectorIndex * 20 + 2, pageWidth - 1, 20, 1);

  for (size_t i = 0; i < files.size(); i++) {
    const auto file = files[i];
    renderer->drawSmallText(50, 50 + i * 20, file.c_str(), i == selectorIndex ? 0 : 1);
  }

  renderer->flushDisplay();
}
