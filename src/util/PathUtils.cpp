#include "PathUtils.h"

namespace PathUtils {

bool containsTraversal(const String& path) {
  // Check for null bytes (could be used to truncate strings in C)
  if (path.indexOf('\0') >= 0) {
    return true;
  }

  // Check for ".." in various forms
  // Direct check for parent directory reference
  if (path.indexOf("..") >= 0) {
    return true;
  }

  // Check for URL-encoded variants (%2e = '.', %2f = '/')
  // %2e%2e = "..", %2e%2e%2f = "../"
  String lower = path;
  lower.toLowerCase();
  if (lower.indexOf("%2e%2e") >= 0 || lower.indexOf("%2e.") >= 0 || lower.indexOf(".%2e") >= 0) {
    return true;
  }

  return false;
}

bool isValidSdPath(const String& path) {
  // Empty paths are invalid
  if (path.isEmpty()) {
    return false;
  }

  // Check length (FAT32 path limit is 255 chars)
  if (path.length() > 255) {
    return false;
  }

  // Must not contain traversal attempts
  if (containsTraversal(path)) {
    return false;
  }

  // Check for null bytes
  for (size_t i = 0; i < path.length(); i++) {
    if (path[i] == '\0') {
      return false;
    }
  }

  return true;
}

String normalizePath(const String& path) {
  if (path.isEmpty()) {
    return "/";
  }

  String result = path;

  // Ensure leading slash
  if (!result.startsWith("/")) {
    result = "/" + result;
  }

  // Collapse multiple consecutive slashes
  while (result.indexOf("//") >= 0) {
    result.replace("//", "/");
  }

  // Remove trailing slash unless it's the root
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }

  return result;
}

namespace {
int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}
}  // namespace

String urlDecode(const String& path) {
  String result;
  result.reserve(path.length());

  for (size_t i = 0; i < path.length(); i++) {
    const char c = path[i];
    if (c == '%' && i + 2 < path.length()) {
      const int hi = hexValue(path[i + 1]);
      const int lo = hexValue(path[i + 2]);
      if (hi >= 0 && lo >= 0) {
        result += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    if (c == '+') {
      result += ' ';
    } else {
      result += c;
    }
  }

  return result;
}

bool isValidFilename(const String& filename) {
  // Empty filenames are invalid
  if (filename.isEmpty()) {
    return false;
  }

  // Check length (FAT32 filename limit)
  if (filename.length() > 255) {
    return false;
  }

  // Must not contain path separators
  if (filename.indexOf('/') >= 0 || filename.indexOf('\\') >= 0) {
    return false;
  }

  // Must not be a traversal attempt
  if (filename == "." || filename == "..") {
    return false;
  }

  // Check for traversal patterns
  if (containsTraversal(filename)) {
    return false;
  }

  // Check for null bytes
  for (size_t i = 0; i < filename.length(); i++) {
    if (filename[i] == '\0') {
      return false;
    }
  }

  return true;
}

}  // namespace PathUtils
