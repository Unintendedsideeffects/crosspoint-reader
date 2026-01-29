#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class SpiBusMutex {
 public:
  static void take();
  static void give();

  class Guard {
   public:
    Guard();
    ~Guard();

   private:
    // Prevent copying
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
  };

 private:
  static SemaphoreHandle_t getMutex();
};
