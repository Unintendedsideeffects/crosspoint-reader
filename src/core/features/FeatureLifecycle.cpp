#include "core/features/FeatureLifecycle.h"

#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <Logging.h>

#if ENABLE_USER_FONTS
#include "CrossPointSettings.h"
#include "UserFontManager.h"
#include "fontIds.h"
#endif

#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
#include "KOReaderCredentialStore.h"
#endif

namespace core {

void FeatureLifecycle::onStorageReady() {
#if ENABLE_USER_FONTS
  UserFontManager::getInstance().scanFonts();
#endif
}

void FeatureLifecycle::onSettingsLoaded(GfxRenderer& renderer) {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
  KOREADER_STORE.loadFromFile();
#endif

#if ENABLE_DARK_MODE
  renderer.setDarkMode(SETTINGS.darkMode);
#endif

#if ENABLE_USER_FONTS
  if (SETTINGS.fontFamily == CrossPointSettings::USER_SD &&
      !UserFontManager::getInstance().loadFontFamily(SETTINGS.userFontPath)) {
    SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
    if (!SETTINGS.saveToFile()) {
      LOG_WRN("FEATURES", "Failed to persist font fallback after user font load failure");
    }
  }
#endif
}

void FeatureLifecycle::onFontSetup(GfxRenderer& renderer) {
#if ENABLE_USER_FONTS
  renderer.insertFontFamily(USER_SD_FONT_ID, UserFontManager::getInstance().getFontFamily());
#endif
}

}  // namespace core
