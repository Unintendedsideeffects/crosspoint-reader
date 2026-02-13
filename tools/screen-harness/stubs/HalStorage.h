#pragma once

#include <cstddef>
#include <cstdint>

using oflag_t = uint16_t;

class FsFile {
 public:
  FsFile() = default;
  explicit operator bool() const { return false; }
  bool seek(uint32_t /*pos*/) { return false; }
  bool seekCur(int32_t /*offset*/) { return false; }
  int read() { return -1; }
  size_t read(void* /*buffer*/, size_t /*count*/) { return 0; }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
};

#define Storage HalStorage::getInstance()
