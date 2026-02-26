#include "core/features/FeatureLifecycle.h"

#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "core/features/FeatureCatalog.h"

#if ENABLE_DARK_MODE || ENABLE_USER_FONTS
#include "CrossPointSettings.h"
#endif

#if ENABLE_USER_FONTS
#include "UserFontManager.h"
#include "fontIds.h"
#endif

#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
#include "KOReaderCredentialStore.h"
#endif

namespace core {
namespace {

using StorageHook = void (*)();
using SettingsHook = void (*)(GfxRenderer&);
using FontHook = void (*)(GfxRenderer&);

struct LifecycleHook {
  const char* featureKey;
  StorageHook onStorageReady;
  SettingsHook onSettingsLoaded;
  FontHook onFontSetup;
};

#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
void koreaderOnSettingsLoaded(GfxRenderer& renderer) {
  (void)renderer;
  KOREADER_STORE.loadFromFile();
}
#endif

#if ENABLE_DARK_MODE
void darkModeOnSettingsLoaded(GfxRenderer& renderer) { renderer.setDarkMode(SETTINGS.darkMode); }
#endif

#if ENABLE_USER_FONTS
void userFontsOnStorageReady() { UserFontManager::getInstance().scanFonts(); }

void userFontsOnSettingsLoaded(GfxRenderer& renderer) {
  (void)renderer;
  if (SETTINGS.fontFamily == CrossPointSettings::USER_SD &&
      !UserFontManager::getInstance().loadFontFamily(SETTINGS.userFontPath)) {
    SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
    if (!SETTINGS.saveToFile()) {
      LOG_WRN("FEATURES", "Failed to persist font fallback after user font load failure");
    }
  }
}

void userFontsOnFontSetup(GfxRenderer& renderer) {
  renderer.insertFontFamily(USER_SD_FONT_ID, UserFontManager::getInstance().getFontFamily());
}
#endif

const LifecycleHook kLifecycleHooks[] = {
#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
    {"koreader_sync", nullptr, koreaderOnSettingsLoaded, nullptr},
#endif
#if ENABLE_DARK_MODE
    {"dark_mode", nullptr, darkModeOnSettingsLoaded, nullptr},
#endif
#if ENABLE_USER_FONTS
    {"user_fonts", userFontsOnStorageReady, userFontsOnSettingsLoaded, userFontsOnFontSetup},
#endif
    {nullptr, nullptr, nullptr, nullptr},
};

template <typename HookInvoker>
void runLifecycleHooks(HookInvoker invoker) {
  for (const auto& hook : kLifecycleHooks) {
    if (hook.featureKey == nullptr || !FeatureCatalog::isEnabled(hook.featureKey)) {
      continue;
    }
    invoker(hook);
  }
}

}  // namespace

void FeatureLifecycle::onStorageReady() {
  runLifecycleHooks([](const LifecycleHook& hook) {
    if (hook.onStorageReady != nullptr) {
      hook.onStorageReady();
    }
  });
}

void FeatureLifecycle::onSettingsLoaded(GfxRenderer& renderer) {
  runLifecycleHooks([&renderer](const LifecycleHook& hook) {
    if (hook.onSettingsLoaded != nullptr) {
      hook.onSettingsLoaded(renderer);
    }
  });
}

void FeatureLifecycle::onFontSetup(GfxRenderer& renderer) {
  runLifecycleHooks([&renderer](const LifecycleHook& hook) {
    if (hook.onFontSetup != nullptr) {
      hook.onFontSetup(renderer);
    }
  });
}

}  // namespace core
