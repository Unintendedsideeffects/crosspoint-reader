#include "features/status_overlay/Registration.h"

#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "core/registries/LifecycleRegistry.h"
#include "fontIds.h"

namespace features::status_overlay {

#if ENABLE_GLOBAL_STATUS_BAR

namespace {

// Draw 1–4 vertical bars representing WiFi signal strength.
// barH: total bar strip height (used to scale individual bars).
void drawSignalBars(const GfxRenderer& renderer, const int x, const int barY, const int barH, const int8_t rssi) {
  // Map RSSI to 1-4 bars: >= -60 = 4, >= -70 = 3, >= -80 = 2, else 1.
  const int bars = rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
  constexpr int barWidth = 3;
  constexpr int barGap = 2;
  for (int i = 0; i < 4; ++i) {
    const int bx = x + i * (barWidth + barGap);
    // Each bar is taller than the previous: min 30%, scaling up to 100% of barH.
    const int bh = (barH * (3 + i)) / (4 + 2);  // proportional heights
    const int by = barY + barH - bh;
    if (i < bars) {
      renderer.fillRect(bx, by, barWidth, bh, true);
    } else {
      renderer.drawRect(bx, by, barWidth, bh, true);
    }
  }
}

void drawStatusOverlay(const GfxRenderer& renderer) {
  if (!SETTINGS.globalStatusBar) {
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int kPadV = 2;
  constexpr int kPadH = 4;
  const int barH = lineH + 2 * kPadV;
  const int barY = (SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM)
                       ? screenH - barH
                       : 0;
  const int sepY = (SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM) ? barY : barY + barH - 1;
  const int textY = barY + kPadV;

  // White background strip.
  renderer.fillRect(0, barY, screenW, barH, false);
  // 1px separator line.
  renderer.drawLine(0, sepY, screenW - 1, sepY, true);

  // --- Battery (left side) ---
  char batBuf[8];
  snprintf(batBuf, sizeof(batBuf), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  renderer.drawText(SMALL_FONT_ID, kPadH, textY, batBuf, true);

  // --- WiFi (right side) ---
  const wifi_mode_t mode = WiFi.getMode();
  const bool isSta = (mode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isAp = (mode & WIFI_MODE_AP);

  if (isSta) {
    constexpr int kBarsW = 4 * (3 + 2) - 2;  // 4 bars * (barWidth+gap) - last gap
    const int barsX = screenW - kBarsW - kPadH;
    drawSignalBars(renderer, barsX, barY + kPadV, lineH, WiFi.RSSI());
  } else if (isAp) {
    const char* apStr = tr(STR_WIFI_AP);
    const int apW = renderer.getTextWidth(SMALL_FONT_ID, apStr);
    renderer.drawText(SMALL_FONT_ID, screenW - apW - kPadH, textY, apStr, true);
  } else {
    const char* noWifi = tr(STR_NO_WIFI);
    const int nwW = renderer.getTextWidth(SMALL_FONT_ID, noWifi);
    renderer.drawText(SMALL_FONT_ID, screenW - nwW - kPadH, textY, noWifi, true);
  }
}

void onSettingsLoaded(GfxRenderer& renderer) {
  renderer.setPostRenderHook(drawStatusOverlay);
}

}  // namespace

#endif  // ENABLE_GLOBAL_STATUS_BAR

void registerFeature() {
#if ENABLE_GLOBAL_STATUS_BAR
  core::LifecycleEntry entry{};
  entry.onSettingsLoaded = onSettingsLoaded;
  core::LifecycleRegistry::add(entry);
#endif
}

}  // namespace features::status_overlay
