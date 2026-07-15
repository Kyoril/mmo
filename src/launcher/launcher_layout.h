// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "geometry.h"

#include "base/typedefs.h"

namespace mmo::layout
{
	/// The launcher is authored once at this size in logical units and scaled to the
	/// physical surface at composite time. Every rectangle below is used by both the
	/// drawing code and the hit testing, so there is exactly one definition of where
	/// anything is.
	constexpr int32 WindowWidth = 900;
	constexpr int32 WindowHeight = 550;

	// --- splash ---------------------------------------------------------------
	/// The splash is drawn full bleed and cover-fitted, so swapping in art of a
	/// different aspect ratio is a content change and not a layout change.
	constexpr Rect Splash{ 0, 0, WindowWidth, WindowHeight };

	/// Crop anchor when the art is wider or taller than the window. Biased above
	/// center vertically so a horizon stays where it belongs instead of being cut.
	constexpr float SplashAnchorX = 0.5f;
	constexpr float SplashAnchorY = 0.35f;

	// --- scrims ---------------------------------------------------------------
	/// Drawn over the splash so that text keeps its contrast regardless of the art
	/// behind it. This does the real legibility work; glyph outlines are insurance.
	constexpr Rect TopScrim{ 0, 0, WindowWidth, 96 };
	constexpr Rect BottomScrim{ 0, 300, WindowWidth, WindowHeight };
	constexpr float VignetteStrength = 0.35f;

	// --- title bar ------------------------------------------------------------
	constexpr Rect Caption{ 0, 0, WindowWidth, 40 };
	constexpr Rect TitleText{ 20, 0, 600, 40 };
	constexpr Rect VersionText{ 600, 12, 808, 32 };
	constexpr Rect MinimizeButton{ 816, 4, 848, 36 };
	constexpr Rect CloseButton{ 856, 4, 888, 36 };

	/// Titlebar icons are square and centered in their button.
	constexpr int32 TitleIconSize = 16;

	// --- bottom bar -----------------------------------------------------------
	/// 98 tall on purpose: that is exactly the source art's height, so at 100% DPI the
	/// panel needs no vertical resampling and only its center stretches horizontally.
	constexpr Rect BottomPanel{ 16, 436, 884, 534 };

	constexpr Rect StatusLabel{ 44, 448, 620, 470 };
	constexpr Rect ProgressTrack{ 44, 478, 620, 506 };
	constexpr Rect PercentText{ 520, 481, 617, 503 };
	constexpr Rect PlayButton{ 648, 452, 868, 518 };
}
