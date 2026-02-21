#include "InputValidation.h"

#include <limits>

namespace InputValidation {

bool findAsciiControlChar(const char* data, const size_t length, size_t& outIndex) {
  if (!data) {
    outIndex = 0;
    return true;
  }

  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c < 0x20 || c == 0x7F) {
      outIndex = i;
      return true;
    }
  }

  return false;
}

bool parseStrictPositiveSize(const char* token, const size_t length, const size_t maxValue, size_t& outValue) {
  if (!token || length == 0) {
    return false;
  }

  size_t value = 0;
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(token[i]);
    if (c < '0' || c > '9') {
      return false;
    }
    const size_t digit = static_cast<size_t>(c - '0');
    if (value > (std::numeric_limits<size_t>::max() - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
  }

  if (value == 0 || value > maxValue) {
    return false;
  }

  outValue = value;
  return true;
}

}  // namespace InputValidation
