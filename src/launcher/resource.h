// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

// Resource identifiers for the launcher. Ranges are reserved by type so that an id
// alone identifies its kind, both for the Win32 RCDATA lookup and for the macOS bundle
// name table in resource_names.h.
//
// NOTE: adding an entry here requires a matching line in launcher.rc, a matching entry
// in resource_names.h, and a matching path in the launcherAssets list in
// CMakeLists.txt. Nothing parses .rc includes, so that list is what makes touching an
// asset actually relink.

// --- Embedded images (RCDATA, PNG): 300-399 ---------------------------------
#define IDR_PNG_SPLASH                  300
#define IDR_PNG_PANEL_BOTTOM            301
#define IDR_PNG_BUTTON_UP               302
#define IDR_PNG_BUTTON_OVER             303
#define IDR_PNG_BUTTON_DOWN             304
#define IDR_PNG_BUTTON_DISABLED         305
#define IDR_PNG_PROGRESS_TRACK          306
#define IDR_PNG_PROGRESS_FILL           307
#define IDR_PNG_ICON_CLOSE              308
#define IDR_PNG_ICON_MINIMIZE           309
#define IDR_PNG_BORDER_FRAME            310
#define IDR_PNG_CONTENT_TEXTURE         311
#define IDR_PNG_HERO_FRAME              312

// --- Embedded fonts (RCDATA, TTF): 400-499 ----------------------------------
#define IDR_TTF_DISPLAY                 400
#define IDR_TTF_BODY                    401
