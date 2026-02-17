#include "CrossPointState.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "SpiBusMutex.h"

namespace {
constexpr uint8_t STATE_FILE_VERSION = 4;
constexpr char STATE_FILE[] = "/.crosspoint/state.bin";
}  // namespace

CrossPointState CrossPointState::instance;

bool CrossPointState::saveToFile() const {
  SpiBusMutex::Guard guard;
  FsFile outputFile;
  if (!Storage.openFileForWrite("CPS", STATE_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, STATE_FILE_VERSION);
  serialization::writeString(outputFile, openEpubPath);
  serialization::writePod(outputFile, lastSleepImage);
  serialization::writePod(outputFile, readerActivityLoadCount);
  serialization::writePod(outputFile, lastSleepFromReader);
  outputFile.close();
  return true;
}

bool CrossPointState::loadFromFile() {
  SpiBusMutex::Guard guard;
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", STATE_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  if (!serialization::readPod(inputFile, version)) {
    LOG_ERR("CPS", "Failed to read version");
    inputFile.close();
    return false;
  }
  if (version > STATE_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    inputFile.close();
    return false;
  }

  if (!serialization::readString(inputFile, openEpubPath)) {
    LOG_ERR("CPS", "Failed to read epub path");
    inputFile.close();
    return false;
  }

  if (version >= 2) {
    if (!serialization::readPod(inputFile, lastSleepImage)) {
      LOG_ERR("CPS", "Failed to read sleep image index");
      inputFile.close();
      return false;
    }
  } else {
    lastSleepImage = 0;
  }

  if (version >= 3) {
    serialization::readPod(inputFile, readerActivityLoadCount);
  }

  if (version >= 4) {
    serialization::readPod(inputFile, lastSleepFromReader);
  } else {
    lastSleepFromReader = false;
  }

  inputFile.close();
  return true;
}
