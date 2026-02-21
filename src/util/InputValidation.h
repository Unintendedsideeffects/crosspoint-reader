#pragma once

#include <cstddef>

namespace InputValidation {

// Returns true when an ASCII control character (<0x20 or 0x7F) is found.
// The first offending byte index is written to outIndex.
bool findAsciiControlChar(const char* data, size_t length, size_t& outIndex);

// Parses a strictly positive decimal integer constrained by maxValue.
// Rejects empty strings, non-digits, overflow, zero, and values > maxValue.
bool parseStrictPositiveSize(const char* token, size_t length, size_t maxValue, size_t& outValue);

}  // namespace InputValidation
