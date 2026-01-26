#pragma once

#include <Arduino.h>

/**
 * Path validation utilities for SD card file operations.
 * Protects against path traversal attacks and normalizes paths.
 */
namespace PathUtils {

/**
 * Check if a path contains directory traversal attempts.
 * Detects patterns like "..", "/..", "../", URL-encoded variants, etc.
 *
 * @param path The path to check
 * @return true if path contains traversal attempts (UNSAFE)
 */
bool containsTraversal(const String& path);

/**
 * Validate that a path is safe for SD card operations.
 * - No traversal attempts
 * - No null bytes
 * - Reasonable length
 *
 * @param path The path to validate
 * @return true if path is valid and safe
 */
bool isValidSdPath(const String& path);

/**
 * Normalize a path for consistent handling.
 * - Ensures leading /
 * - Removes trailing / (except for root)
 * - Collapses multiple consecutive slashes
 *
 * @param path The path to normalize
 * @return Normalized path, or empty string if invalid
 */
String normalizePath(const String& path);

/**
 * Validate a filename (no path separators or traversal).
 * Used for uploaded filenames before combining with destination path.
 *
 * @param filename The filename to validate
 * @return true if filename is valid
 */
bool isValidFilename(const String& filename);

}  // namespace PathUtils
