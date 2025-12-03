#pragma once
#include <string>

class ZipFile {
  std::string filePath;

 public:
  explicit ZipFile(std::string filePath) : filePath(std::move(filePath)) {}
  ~ZipFile() = default;
  char* readTextFileToMemory(const char* filename, size_t* size = nullptr) const;
  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false) const;
};
