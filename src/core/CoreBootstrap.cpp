#include "core/CoreBootstrap.h"

#include <Logging.h>

#include "core/features/FeatureCatalog.h"

namespace core {
namespace {
FeatureSystemStatus gFeatureStatus{false, false, ""};
}  // namespace

void CoreBootstrap::initializeFeatureSystem(const bool logSummary) {
  gFeatureStatus.initialized = true;
  gFeatureStatus.validationError = "";
  gFeatureStatus.dependencyGraphValid = FeatureCatalog::validate(&gFeatureStatus.validationError);

  if (!gFeatureStatus.dependencyGraphValid) {
    LOG_ERR("CORE", "Feature dependency validation failed: %s", gFeatureStatus.validationError.c_str());
  } else if (logSummary) {
    LOG_INF("CORE", "Feature dependency validation passed");
  }

  if (logSummary) {
    FeatureCatalog::printToSerial();
  }
}

const FeatureSystemStatus& CoreBootstrap::getFeatureSystemStatus() { return gFeatureStatus; }

}  // namespace core
