#pragma once

// Central source of truth for compile-time feature toggles.
// Build flags (e.g. -DENABLE_MARKDOWN=0) override these defaults.

#ifndef ENABLE_EXTENDED_FONTS
#define ENABLE_EXTENDED_FONTS 1
#endif

#ifndef ENABLE_IMAGE_SLEEP
#define ENABLE_IMAGE_SLEEP 1
#endif

#ifndef ENABLE_MARKDOWN
#define ENABLE_MARKDOWN 1
#endif

#ifndef ENABLE_INTEGRATIONS
#define ENABLE_INTEGRATIONS 1
#endif

#ifndef ENABLE_KOREADER_SYNC
#define ENABLE_KOREADER_SYNC 0
#endif

#ifndef ENABLE_CALIBRE_SYNC
#define ENABLE_CALIBRE_SYNC 0
#endif

#ifndef ENABLE_BACKGROUND_SERVER
#define ENABLE_BACKGROUND_SERVER 1
#endif

// Enforce downstream dependencies at compile time.
#if !ENABLE_INTEGRATIONS
#undef ENABLE_KOREADER_SYNC
#define ENABLE_KOREADER_SYNC 0
#undef ENABLE_CALIBRE_SYNC
#define ENABLE_CALIBRE_SYNC 0
#endif
