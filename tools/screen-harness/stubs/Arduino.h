#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <thread>

#ifndef PROGMEM
#define PROGMEM
#endif

template <typename T>
inline uint8_t pgm_read_byte(const T* ptr) {
  return static_cast<uint8_t>(*ptr);
}

constexpr uint8_t HIGH = 1;
constexpr uint8_t LOW = 0;
constexpr uint8_t INPUT = 0;
constexpr uint8_t OUTPUT = 1;
constexpr uint8_t INPUT_PULLUP = 2;

using byte = uint8_t;

inline unsigned long millis() {
  static const auto kStart = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - kStart;
  return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

inline void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline void pinMode(int /*pin*/, int /*mode*/) {}

inline void digitalWrite(int /*pin*/, int /*value*/) {}

inline int digitalRead(int /*pin*/) { return LOW; }

class HardwareSerial {
 public:
  explicit operator bool() const { return false; }

  void printf(const char* /*fmt*/, ...) const {
    // Intentionally silent for deterministic host harness output.
  }

  void println(const char* /*text*/) const {
    // Intentionally silent for deterministic host harness output.
  }
};

extern HardwareSerial Serial;
