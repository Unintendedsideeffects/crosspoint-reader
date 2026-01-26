#pragma once

#include <SDCardManager.h>

/**
 * RAII wrapper for FsFile that ensures the file is closed on scope exit.
 *
 * Follows the SpiBusMutex::Guard pattern for consistent resource management.
 *
 * Usage:
 *   FsFile file;
 *   SdMan.openFileForRead("TAG", path, file);
 *   FsFileGuard guard(file);
 *   // ... use file ...
 *   // file is automatically closed when guard goes out of scope
 *
 * For success paths where the caller takes ownership of the file,
 * call release() to prevent automatic closing:
 *   FsFileGuard guard(file);
 *   // ... process file ...
 *   guard.release();  // caller now owns the file handle
 *   return file;
 */
class FsFileGuard {
 public:
  /**
   * Construct a guard that will close the file on destruction.
   * @param file Reference to the FsFile to manage
   */
  explicit FsFileGuard(FsFile& file) : file_(file), released_(false) {}

  /**
   * Destructor - closes the file unless release() was called.
   */
  ~FsFileGuard() {
    if (!released_ && file_) {
      file_.close();
    }
  }

  // Prevent copying - file ownership must be explicit
  FsFileGuard(const FsFileGuard&) = delete;
  FsFileGuard& operator=(const FsFileGuard&) = delete;

  /**
   * Release the file from the guard's management.
   * After calling this, the destructor will not close the file.
   * Use this when transferring ownership to the caller.
   */
  void release() { released_ = true; }

  /**
   * Access the underlying file reference.
   */
  FsFile& get() { return file_; }
  const FsFile& get() const { return file_; }

  /**
   * Check if the file is valid (open and usable).
   */
  explicit operator bool() const { return static_cast<bool>(file_); }

 private:
  FsFile& file_;
  bool released_;
};
