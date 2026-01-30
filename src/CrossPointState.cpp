#include "CrossPointState.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include "SpiBusMutex.h"

namespace {
constexpr uint8_t STATE_FILE_VERSION = 2;
constexpr char STATE_FILE[] = "/.crosspoint/state.bin";
}  // namespace

CrossPointState CrossPointState::instance;

bool CrossPointState::saveToFile() const {
  SpiBusMutex::Guard guard;
  FsFile outputFile;
  if (!SdMan.openFileForWrite("CPS", STATE_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, STATE_FILE_VERSION);
  serialization::writeString(outputFile, openEpubPath);
  serialization::writePod(outputFile, lastSleepImage);
  outputFile.close();
  return true;
}

bool CrossPointState::loadFromFile() {
  SpiBusMutex::Guard guard;
  FsFile inputFile;
  if (!SdMan.openFileForRead("CPS", STATE_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  if (!serialization::readPod(inputFile, version)) {
    Serial.printf("[%lu] [CPS] Failed to read version\n", millis());
    inputFile.close();
    return false;
  }
  if (version > STATE_FILE_VERSION) {
    Serial.printf("[%lu] [CPS] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return false;
  }

  if (!serialization::readString(inputFile, openEpubPath)) {
    Serial.printf("[%lu] [CPS] Failed to read epub path\n", millis());
    inputFile.close();
    return false;
  }

  if (version >= 2) {
    if (!serialization::readPod(inputFile, lastSleepImage)) {
      Serial.printf("[%lu] [CPS] Failed to read sleep image index\n", millis());
      inputFile.close();
      return false;
    }
  } else {
    lastSleepImage = 0;
  }

  inputFile.close();
  return true;
}
