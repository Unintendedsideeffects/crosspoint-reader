#include "PathUtils.h"

namespace PathUtils {

bool containsTraversal(const String& path) {
  // Note: null byte check removed - indexOf('\0') finds the internal C-string terminator
  // Null bytes are checked separately in isValidSdPath via character iteration

  // Check for ".." as a path component (actual traversal), not just any ".." in filename
  // Patterns that indicate traversal:
  // - "/../" anywhere in path
  // - "/.." at end of path
  // - "../" at start of path
  // - path equals ".."
  if (path.indexOf("/../") >= 0) {
    Serial.printf("[PathUtils] traversal: /../\n");
    return true;
  }
  if (path.endsWith("/..")) {
    Serial.printf("[PathUtils] traversal: ends /.. \n");
    return true;
  }
  if (path.startsWith("../")) {
    Serial.printf("[PathUtils] traversal: starts ../\n");
    return true;
  }
  if (path == "..") {
    Serial.printf("[PathUtils] traversal: equals ..\n");
    return true;
  }

  // Check for URL-encoded traversal variants (in case called before decoding)
  // %2e = '.', %2f = '/'
  String lower = path;
  lower.toLowerCase();
  if (lower.indexOf("%2e%2e%2f") >= 0) {
    Serial.printf("[PathUtils] traversal: %%2e%%2e%%2f\n");
    return true;
  }
  if (lower.indexOf("%2f%2e%2e") >= 0) {
    Serial.printf("[PathUtils] traversal: %%2f%%2e%%2e\n");
    return true;
  }
  if (lower.indexOf("..%2f") >= 0) {
    Serial.printf("[PathUtils] traversal: ..%%2f\n");
    return true;
  }
  if (lower.indexOf("%2f..") >= 0) {
    Serial.printf("[PathUtils] traversal: %%2f..\n");
    return true;
  }

  return false;
}

bool isValidSdPath(const String& path) {
  // Empty paths are invalid
  if (path.isEmpty()) {
    Serial.printf("[PathUtils] REJECT: empty path\n");
    return false;
  }

  // Check length (FAT32 path limit is 255 chars)
  if (path.length() > 255) {
    Serial.printf("[PathUtils] REJECT: path too long (%d chars)\n", path.length());
    return false;
  }

  // Must not contain traversal attempts
  if (containsTraversal(path)) {
    Serial.printf("[PathUtils] REJECT: traversal in '%s'\n", path.c_str());
    return false;
  }

  // Check for null bytes
  for (size_t i = 0; i < path.length(); i++) {
    if (path[i] == '\0') {
      Serial.printf("[PathUtils] REJECT: null at %d\n", i);
      return false;
    }
    if (path[i] == '\\') {
      Serial.printf("[PathUtils] REJECT: backslash at %d\n", i);
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

  // Reject filesystem-invalid characters
  for (size_t i = 0; i < filename.length(); i++) {
    const char c = filename[i];
    if (c == '"' || c == '*' || c == ':' || c == '<' || c == '>' || c == '?' || c == '|') {
      return false;
    }
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
