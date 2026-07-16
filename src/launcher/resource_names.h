// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "resource.h"

#include "base/typedefs.h"

namespace mmo
{
	/// Maps a resource id to the file name it ships under outside of Windows.
	///
	/// Win32 embeds these as RCDATA and looks them up by id, so it never needs this
	/// table. A macOS build has no equivalent of RCDATA and would copy the same files
	/// into the bundle's Resources directory instead, resolving them by name. Keeping
	/// the table here, next to the id list, is what lets the portable code stay
	/// unaware of which mechanism is in use.
	struct ResourceName
	{
		uint32 id;
		const char* fileName;
	};

	/// Must stay in sync with the RCDATA block in launcher.rc, in both content and order.
	constexpr ResourceName ResourceNames[] =
	{
		{ IDR_PNG_SPLASH,           "splash.png" },
		{ IDR_PNG_PANEL_BOTTOM,     "panel_bottom.png" },
		{ IDR_PNG_BUTTON_UP,        "button_up.png" },
		{ IDR_PNG_BUTTON_OVER,      "button_over.png" },
		{ IDR_PNG_BUTTON_DOWN,      "button_down.png" },
		{ IDR_PNG_BUTTON_DISABLED,  "button_disabled.png" },
		{ IDR_PNG_PROGRESS_TRACK,   "progress_track.png" },
		{ IDR_PNG_PROGRESS_FILL,    "progress_fill.png" },
		{ IDR_PNG_ICON_CLOSE,       "icon_close.png" },
		{ IDR_PNG_ICON_MINIMIZE,    "icon_minimize.png" },
		{ IDR_PNG_BORDER_FRAME,     "border_frame.png" },
		{ IDR_PNG_CONTENT_TEXTURE,  "content_texture.png" },
		{ IDR_PNG_HERO_FRAME,       "hero_frame.png" },
		{ IDR_TTF_DISPLAY,          "font_display.ttf" },
		{ IDR_TTF_BODY,             "font_body.ttf" },
	};
}
