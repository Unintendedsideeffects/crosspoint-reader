#include "RecentBooksStore.h"

#include <Epub.h>
#include <HalStorage.h>
#include <HardwareSerial.h>
#include <Serialization.h>
#include <Xtc.h>

#include <algorithm>

#include "SpiBusMutex.h"
#include "util/StringUtils.h"

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 3;
constexpr char RECENT_BOOKS_FILE[] = "/.crosspoint/recent.bin";
constexpr int MAX_RECENT_BOOKS = 10;
}  // namespace

RecentBooksStore RecentBooksStore::instance;

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath) {
  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
  }

  // Add to front
  recentBooks.insert(recentBooks.begin(), {path, title, author, coverBmpPath});

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

bool RecentBooksStore::saveToFile() const {
  SpiBusMutex::Guard guard;
  // Make sure the directory exists
  Storage.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!Storage.openFileForWrite("RBS", RECENT_BOOKS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, RECENT_BOOKS_FILE_VERSION);
  const uint8_t count = static_cast<uint8_t>(recentBooks.size());
  serialization::writePod(outputFile, count);

  for (const auto& book : recentBooks) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
    serialization::writeString(outputFile, book.coverBmpPath);
  }

  outputFile.close();
  Serial.printf("[%lu] [RBS] Recent books saved to file (%d entries)\n", millis(), count);
  return true;
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  Serial.printf("Loading recent book: %s\n", path.c_str());

  // If epub, try to load the metadata for title/author and cover
  if (StringUtils::checkFileExtension(lastBookFileName, ".epub")) {
    Epub epub(path, "/.crosspoint");
    epub.load(false);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(), epub.getThumbBmpPath()};
  } else if (StringUtils::checkFileExtension(lastBookFileName, ".xtch") ||
             StringUtils::checkFileExtension(lastBookFileName, ".xtc")) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{path, xtc.getTitle(), xtc.getAuthor(), xtc.getThumbBmpPath()};
    }
  } else if (StringUtils::checkFileExtension(lastBookFileName, ".txt") ||
             StringUtils::checkFileExtension(lastBookFileName, ".md")) {
    return RecentBook{path, lastBookFileName, "", ""};
  }
  return RecentBook{path, "", "", ""};
}

bool RecentBooksStore::loadFromFile() {
  SpiBusMutex::Guard guard;
  FsFile inputFile;
  if (!Storage.openFileForRead("RBS", RECENT_BOOKS_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  if (!serialization::readPod(inputFile, version)) {
    Serial.printf("[%lu] [RBS] Failed to read version\n", millis());
    inputFile.close();
    return false;
  }

  if (version != RECENT_BOOKS_FILE_VERSION) {
    if (version == 1 || version == 2) {
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
      std::string path, title, author, coverBmpPath;
      if (!serialization::readString(inputFile, path) || !serialization::readString(inputFile, title) ||
          !serialization::readString(inputFile, author) || !serialization::readString(inputFile, coverBmpPath)) {
        Serial.printf("[%lu] [RBS] Failed to read book entry %d\n", millis(), i);
        inputFile.close();
        return false;
      }
      recentBooks.push_back({path, title, author, coverBmpPath});
    }
  }

  inputFile.close();
  Serial.printf("[%lu] [RBS] Recent books loaded from file (%d entries)\n", millis(), recentBooks.size());
  return true;
}
