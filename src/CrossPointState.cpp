#include "CrossPointState.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>

namespace {
constexpr uint8_t STATE_FILE_VERSION = 4;
constexpr char STATE_FILE_BIN[] = "/.crosspoint/state.bin";
constexpr char STATE_FILE_JSON[] = "/.crosspoint/state.json";
constexpr char STATE_FILE_BAK[] = "/.crosspoint/state.bin.bak";
}  // namespace

CrossPointState CrossPointState::instance;

bool CrossPointState::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveState(*this, STATE_FILE_JSON);
}

bool CrossPointState::loadFromFile() {
  // Try JSON first
  if (Storage.exists(STATE_FILE_JSON)) {
    String json = Storage.readFile(STATE_FILE_JSON);
    if (!json.isEmpty()) {
      return JsonSettingsIO::loadState(*this, json.c_str());
    }
  }

  // Fall back to binary migration
  if (Storage.exists(STATE_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      if (saveToFile()) {
        Storage.rename(STATE_FILE_BIN, STATE_FILE_BAK);
        LOG_DBG("CPS", "Migrated state.bin to state.json");
        return true;
      } else {
        LOG_ERR("CPS", "Failed to save state during migration");
      }
    }
  }

  return false;
}

bool CrossPointState::loadFromBinaryFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", STATE_FILE_BIN, inputFile)) {
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
    if (!serialization::readPod(inputFile, readerActivityLoadCount)) {
      LOG_ERR("CPS", "Failed to read reader activity counter");
      inputFile.close();
      return false;
    }
  }

  if (version >= 4) {
    if (!serialization::readPod(inputFile, lastSleepFromReader)) {
      LOG_ERR("CPS", "Failed to read sleep source flag");
      inputFile.close();
      return false;
    }
  } else {
    lastSleepFromReader = false;
  }

  inputFile.close();
  return true;
}
