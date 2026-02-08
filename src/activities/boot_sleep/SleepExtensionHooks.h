#pragma once

class GfxRenderer;
class MappedInputManager;

namespace SleepExtensionHooks {

/**
 * Optional extension hook for external sleep applications.
 *
 * A weak default implementation exists and returns false.
 * Provide a strong implementation in another translation unit to override the
 * built-in sleep rendering flow (for example, a TRMNL-style dashboard renderer).
 *
 * Return true when rendering was fully handled and SleepActivity should stop.
 */
bool renderExternalSleepScreen(GfxRenderer& renderer, MappedInputManager& mappedInput);

}  // namespace SleepExtensionHooks
