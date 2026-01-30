#include "RecentBooksStore.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>

#include "SpiBusMutex.h"

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 2;
constexpr char RECENT_BOOKS_FILE[] = "/.crosspoint/recent.bin";
constexpr int MAX_RECENT_BOOKS = 10;
}  // namespace

RecentBooksStore RecentBooksStore::instance;

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author) {
  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
  }

  // Add to front
  recentBooks.insert(recentBooks.begin(), {path, title, author});

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

bool RecentBooksStore::saveToFile() const {
  SpiBusMutex::Guard guard;
  // Make sure the directory exists
  SdMan.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("RBS", RECENT_BOOKS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, RECENT_BOOKS_FILE_VERSION);
  const uint8_t count = static_cast<uint8_t>(recentBooks.size());
  serialization::writePod(outputFile, count);

  for (const auto& book : recentBooks) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
  }

  outputFile.close();
  Serial.printf("[%lu] [RBS] Recent books saved to file (%d entries)\n", millis(), count);
  return true;
}

bool RecentBooksStore::loadFromFile() {
  SpiBusMutex::Guard guard;
  FsFile inputFile;
  if (!SdMan.openFileForRead("RBS", RECENT_BOOKS_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  if (!serialization::readPod(inputFile, version)) {
    Serial.printf("[%lu] [RBS] Failed to read version\n", millis());
    inputFile.close();
    return false;
  }

  if (version != RECENT_BOOKS_FILE_VERSION) {
    if (version == 1) {
      // Old version, just read paths
      uint8_t count;
      if (!serialization::readPod(inputFile, count)) {
        Serial.printf("[%lu] [RBS] Failed to read count\n", millis());
        inputFile.close();
        return false;
      }
      recentBooks.clear();
      recentBooks.reserve(count);
      for (uint8_t i = 0; i < count; i++) {
        std::string path;
        if (!serialization::readString(inputFile, path)) {
          Serial.printf("[%lu] [RBS] Failed to read path\n", millis());
          inputFile.close();
          return false;
        }
        // Title and author will be empty, they will be filled when the book is
        // opened again
        recentBooks.push_back({path, "", ""});
      }
    } else {
      Serial.printf("[%lu] [RBS] Deserialization failed: Unknown version %u\n", millis(), version);
      inputFile.close();
      return false;
    }
  } else {
    uint8_t count;
    if (!serialization::readPod(inputFile, count)) {
      Serial.printf("[%lu] [RBS] Failed to read count\n", millis());
      inputFile.close();
      return false;
    }

    recentBooks.clear();
    recentBooks.reserve(count);

    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author;
      if (!serialization::readString(inputFile, path) || !serialization::readString(inputFile, title) ||
          !serialization::readString(inputFile, author)) {
        Serial.printf("[%lu] [RBS] Failed to read book entry %d\n", millis(), i);
        inputFile.close();
        return false;
      }
      recentBooks.push_back({path, title, author});
    }
  }

  inputFile.close();
  Serial.printf("[%lu] [RBS] Recent books loaded from file (%d entries)\n", millis(), recentBooks.size());
  return true;
}