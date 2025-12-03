#include "CrossPointState.h"

#include <HardwareSerial.h>
#include <SD.h>
#include <Serialization.h>

#include <fstream>

constexpr uint8_t STATE_VERSION = 1;
constexpr char STATE_FILE[] = "/sd/.crosspoint/state.bin";

void CrossPointState::serialize(std::ostream& os) const {
  serialization::writePod(os, STATE_VERSION);
  serialization::writeString(os, openEpubPath);
}

CrossPointState* CrossPointState::deserialize(std::istream& is) {
  const auto state = new CrossPointState();

  uint8_t version;
  serialization::readPod(is, version);
  if (version != STATE_VERSION) {
    Serial.printf("CrossPointState: Unknown version %u\n", version);
    return state;
  }

  serialization::readString(is, state->openEpubPath);
  return state;
}

void CrossPointState::saveToFile() const {
  std::ofstream outputFile(STATE_FILE);
  serialize(outputFile);
  outputFile.close();
}

CrossPointState* CrossPointState::loadFromFile() {
  if (!SD.exists(&STATE_FILE[3])) {
    return new CrossPointState();
  }

  std::ifstream inputFile(STATE_FILE);
  CrossPointState* state = deserialize(inputFile);
  inputFile.close();
  return state;
}
