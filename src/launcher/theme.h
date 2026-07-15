// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "color.h"
#include "geometry.h"
#include "resource.h"

#include "base/typedefs.h"

namespace mmo
{
	/// A nine sliceable asset and the insets that describe its non-stretchable border.
	struct NineSliceDef
	{
		uint32 resourceId;
		/// In source image pixels. Scaled with DPI at draw time, otherwise corners
		/// would shrink relative to the frame as the window grows.
		Insets insets;
		/// Tile the edges instead of stretching them. Every asset used here has
		/// gradient edges rather than a repeating motif, so stretching is invisible
		/// while tiling would produce a visible seam.
		bool tileEdges;
	};

	/// Every color, size and asset the launcher skin uses. Changing the look should
	/// mean changing this file and nothing else.
	namespace theme
	{
		// --- nine slice assets -------------------------------------------------
		//
		// These insets were measured from the art rather than guessed. Detecting them
		// automatically is not viable: comparing adjacent columns is exact for flat
		// frames but returns garbage on the noise-textured panel, and the answer swings
		// wildly with the tolerance. So they are pinned here, where a wrong value is
		// visible in a second.

		/// fg4_borders_01_11, 712x98. Ornamental metal caps occupy roughly x<44 and
		/// x>668; the bevel rails occupy y<18 and y>80. The center is flat noise, so it
		/// stretches rather than tiles.
		constexpr NineSliceDef PanelBottom{ IDR_PNG_PANEL_BOTTOM, { 44, 18, 44, 18 }, false };

		/// fg4_buttonBrown1_*, 72x72. The Up and Disabled states measure 14 and Down
		/// measures 16 (a thicker pressed bevel). All four share 16 so that the button
		/// geometry does not shift when the state changes; on Up the extra band is flat
		/// fill and the difference is invisible.
		constexpr NineSliceDef ButtonUp{ IDR_PNG_BUTTON_UP, { 16, 16, 16, 16 }, false };
		constexpr NineSliceDef ButtonOver{ IDR_PNG_BUTTON_OVER, { 16, 16, 16, 16 }, false };
		constexpr NineSliceDef ButtonDown{ IDR_PNG_BUTTON_DOWN, { 16, 16, 16, 16 }, false };
		constexpr NineSliceDef ButtonDisabled{ IDR_PNG_BUTTON_DISABLED, { 16, 16, 16, 16 }, false };

		/// fg4_borders_insetBlack, 66x66. A soft inner shadow with a fully transparent
		/// center, not an opaque frame -- it only reads as a recess when drawn on top
		/// of a filled base, which is what DrawProgressBar does.
		constexpr NineSliceDef ProgressTrack{ IDR_PNG_PROGRESS_TRACK, { 15, 15, 15, 15 }, false };

		/// fg4_progressbar1, 72x72. A rounded pill with a vertical gradient.
		constexpr NineSliceDef ProgressFill{ IDR_PNG_PROGRESS_FILL, { 9, 9, 9, 9 }, false };

		// --- colors ------------------------------------------------------------
		constexpr Color TitleColor = FromArgb(0xFFE8D8B0);
		constexpr Color TitleOutline = FromArgb(0xC0000000);
		constexpr Color VersionColor = FromArgb(0x88E8D8B0);

		constexpr Color StatusColor = FromArgb(0xFFD8C7A4);
		constexpr Color StatusErrorColor = FromArgb(0xFFE08A70);
		constexpr Color PercentColor = FromArgb(0xFFF0E2C0);
		constexpr Color TextShadow = FromArgb(0xB4000000);

		constexpr Color PlayTextEnabled = FromArgb(0xFFF6E7C0);
		constexpr Color PlayTextDisabled = FromArgb(0x80A09070);
		constexpr Color PlayTextOutline = FromArgb(0xD0140D06);

		/// The recess drawn under the progress track's shadow overlay.
		constexpr Color ProgressBase = FromArgb(0xFF120C08);
		/// Warm gold, modulated onto the fill pill.
		constexpr Color ProgressTint = FromArgb(0xFFFFC94A);

		/// Hover and press washes for the titlebar icon buttons, which have no art.
		constexpr Color IconButtonHover = FromArgb(0x33FFFFFF);
		constexpr Color IconButtonPressed = FromArgb(0x55000000);
		constexpr Color IconTint = FromArgb(0xFFE8D8B0);

		/// Scrim ramps. Both ends are premultiplied, so they interpolate cleanly to
		/// fully transparent without fringing.
		constexpr Color TopScrimFrom = FromArgb(0xBE000000);
		constexpr Color TopScrimTo = FromArgb(0x00000000);
		constexpr Color BottomScrimFrom = FromArgb(0x00000000);
		constexpr Color BottomScrimTo = FromArgb(0xEB080604);

		/// Shown before the splash is available, and behind it if decoding ever fails.
		constexpr Color WindowBackground = FromArgb(0xFF17110B);

		// --- fonts -------------------------------------------------------------
		/// Logical pixel heights; scaled by DPI when the faces are built.
		constexpr int32 TitleFontSize = 15;
		constexpr int32 VersionFontSize = 11;
		constexpr int32 StatusFontSize = 13;
		constexpr int32 PercentFontSize = 12;
		constexpr int32 PlayFontSize = 26;

		// --- motion ------------------------------------------------------------
		/// Seconds for a button's hover glow to fade fully in or out.
		constexpr float HoverFadeDuration = 0.12f;
		/// How quickly the progress bar eases toward its target, per second. The
		/// updater reports progress in per-file jumps; easing turns that into a bar
		/// that moves instead of teleporting.
		constexpr float ProgressEaseRate = 8.0f;
	}
}
