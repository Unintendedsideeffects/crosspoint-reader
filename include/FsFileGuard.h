#pragma once

#include <SDCardManager.h>

class FsFileGuard {
 public:
  explicit FsFileGuard(FsFile& file) : file_(file), released_(false) {}

  ~FsFileGuard() {
    if (!released_ && file_) {
      file_.close();
    }
  }

  FsFileGuard(const FsFileGuard&) = delete;
  FsFileGuard& operator=(const FsFileGuard&) = delete;

  void release() { released_ = true; }

  FsFile& get() { return file_; }
  const FsFile& get() const { return file_; }

  explicit operator bool() const { return static_cast<bool>(file_); }

 private:
  FsFile& file_;
  bool released_;
};
