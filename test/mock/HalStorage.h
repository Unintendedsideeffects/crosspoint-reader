#pragma once
// Host test stub — replaces the real HalStorage/FsFile with an
// in-memory implementation suitable for unit testing.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ── In-memory FsFile ─────────────────────────────────────────────────────
class FsFile {
 public:
  FsFile() = default;

  static FsFile forWrite(std::shared_ptr<std::vector<uint8_t>> buf) {
    FsFile f;
    f.buf_ = buf;
    f.pos_ = 0;
    return f;
  }
  static FsFile forRead(std::shared_ptr<std::vector<uint8_t>> buf) { return forWrite(buf); }

  void write(const uint8_t* data, size_t len) {
    if (buf_) buf_->insert(buf_->end(), data, data + len);
  }

  size_t read(uint8_t* data, size_t len) {
    if (!buf_) return 0;
    const size_t avail = buf_->size() - pos_;
    const size_t n = std::min(len, avail);
    std::memcpy(data, buf_->data() + pos_, n);
    pos_ += n;
    return n;
  }

  void close() {}
  explicit operator bool() const { return buf_ != nullptr; }

 private:
  std::shared_ptr<std::vector<uint8_t>> buf_;
  size_t pos_ = 0;
};

// ── In-memory HalStorage singleton ──────────────────────────────────────
class HalStorage {
 public:
  bool mkdir(const char*) { return true; }
  bool mkdir(const char*, bool) { return true; }

  bool openFileForWrite(const char* /*tag*/, const char* path, FsFile& file) {
    auto buf = std::make_shared<std::vector<uint8_t>>();
    files_[path] = buf;
    file = FsFile::forWrite(buf);
    return true;
  }
  bool openFileForWrite(const char* tag, const std::string& path, FsFile& file) {
    return openFileForWrite(tag, path.c_str(), file);
  }

  bool openFileForRead(const char* /*tag*/, const char* path, FsFile& file) {
    auto it = files_.find(path);
    if (it == files_.end()) return false;
    file = FsFile::forRead(it->second);
    return true;
  }
  bool openFileForRead(const char* tag, const std::string& path, FsFile& file) {
    return openFileForRead(tag, path.c_str(), file);
  }

  // Unused in tests but referenced by CrossPointSettings.h surface
  bool exists(const char*) { return false; }
  bool remove(const char*) { return true; }

  static HalStorage& getInstance() {
    static HalStorage inst;
    return inst;
  }

  // Reset between tests
  void reset() { files_.clear(); }

 private:
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files_;
};

#define Storage HalStorage::getInstance()
