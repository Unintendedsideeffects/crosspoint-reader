#include "Logging.h"

#include <cstdarg>
#include <cstdio>

MySerialImpl MySerialImpl::instance;

size_t MySerialImpl::printf(const char* format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  const int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (len <= 0) {
    return 0;
  }
  return logSerial.print(buf);
}

size_t MySerialImpl::write(const uint8_t b) { return logSerial.write(b); }

size_t MySerialImpl::write(const uint8_t* buffer, const size_t size) { return logSerial.write(buffer, size); }

void MySerialImpl::flush() { logSerial.flush(); }

// Since logging can take a large amount of flash, we want to make the format string as short as possible.
// This logPrintf prepend the timestamp, level and origin to the user-provided message, so that the user only needs to
// provide the format string for the message itself.
void logPrintf(const char* level, const char* origin, const char* format, ...) {
  if (!logSerial) {
    return;  // Serial not initialized, skip logging
  }
  va_list args;
  va_start(args, format);
  char buf[256];
  char* c = buf;
  // add the timestamp
  {
    unsigned long ms = millis();
    int len = snprintf(c, sizeof(buf), "[%lu] ", ms);
    if (len < 0) {
      return;  // encoding error, skip logging
    }
    c += len;
  }
  // add the level
  {
    const char* p = level;
    size_t remaining = sizeof(buf) - (c - buf);
    while (*p && remaining > 1) {
      *c++ = *p++;
      remaining--;
    }
    if (remaining > 1) {
      *c++ = ' ';
    }
  }
  // add the origin
  {
    int len = snprintf(c, sizeof(buf) - (c - buf), "[%s] ", origin);
    if (len < 0) {
      return;  // encoding error, skip logging
    }
    c += len;
  }
  // add the user message
  vsnprintf(c, sizeof(buf) - (c - buf), format, args);
  va_end(args);
  logSerial.print(buf);
}
