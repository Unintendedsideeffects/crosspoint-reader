#include "SleepExtensionHooks.h"

namespace SleepExtensionHooks {

bool __attribute__((weak)) renderExternalSleepScreen(GfxRenderer& /*renderer*/, MappedInputManager& /*mappedInput*/) {
  return false;
}

}  // namespace SleepExtensionHooks
