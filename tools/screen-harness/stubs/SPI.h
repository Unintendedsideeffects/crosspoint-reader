#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint8_t MSBFIRST = 1;
constexpr uint8_t SPI_MODE0 = 0;

class SPISettings {
 public:
  SPISettings() = default;
  SPISettings(uint32_t /*clock*/, uint8_t /*bitOrder*/, uint8_t /*mode*/) {}
};

class SPIClass {
 public:
  void begin(int8_t /*sclk*/, int8_t /*miso*/, int8_t /*mosi*/, int8_t /*ss*/) {}

  void beginTransaction(const SPISettings& /*settings*/) {}

  void endTransaction() {}

  uint8_t transfer(uint8_t data) { return data; }

  void writeBytes(const uint8_t* /*data*/, uint16_t /*size*/) {}
};

extern SPIClass SPI;
