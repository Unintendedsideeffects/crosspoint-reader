#pragma once

#include <Arduino.h>

#include <cstddef>

namespace core {

struct FeatureDescriptor {
  const char* key;
  const char* label;
  bool enabled;
  const char* const* requiresAll;
  size_t requiresAllCount;
  const char* const* requiresAny;
  size_t requiresAnyCount;
};

class FeatureCatalog {
 public:
  static const FeatureDescriptor* all(size_t& count);
  static const FeatureDescriptor* find(const char* key);
  static bool isEnabled(const char* key);
  static int enabledCount();
  static size_t totalCount();
  static String buildString();
  static String toJson();
  static bool validate(String* error = nullptr);
  static void printToSerial();
};

}  // namespace core
