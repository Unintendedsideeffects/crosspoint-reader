#pragma once

#include <Arduino.h>

namespace core {

struct FeatureSystemStatus {
  bool initialized = false;
  bool dependencyGraphValid = false;
  String validationError = "";
};

class CoreBootstrap {
 public:
  static void initializeFeatureSystem(bool logSummary);
  static const FeatureSystemStatus& getFeatureSystemStatus();
};

}  // namespace core
