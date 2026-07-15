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

	// --- window frame ---------------------------------------------------------
	/// The ornate border wraps the whole window; all content sits inside its opening.
	constexpr Rect WindowFrame{ 0, 0, WindowWidth, WindowHeight };

	/// How far the frame art reaches in from each edge. Content is inset by this.
	constexpr int32 FrameThickness = 48;

	/// The frame's inner opening. Everything below lives inside it, and the splash is
	/// clipped to it: outside the frame art's silhouette the window is a real hole
	/// through to the desktop, so anything drawn out there would fill it back in.
	constexpr Rect Content{
		FrameThickness, FrameThickness,
		WindowWidth - FrameThickness, WindowHeight - FrameThickness
	};

	// --- splash ---------------------------------------------------------------
	/// The splash is cover-fitted to the window and clipped to the frame's opening, so
	/// swapping in art of a different aspect ratio is a content change and not a layout
	/// change.
	constexpr Rect Splash{ 0, 0, WindowWidth, WindowHeight };

	/// Crop anchor when the art is wider or taller than the window. Biased above
	/// center vertically so a horizon stays where it belongs instead of being cut.
	constexpr float SplashAnchorX = 0.5f;
	constexpr float SplashAnchorY = 0.35f;

	// --- scrims ---------------------------------------------------------------
	/// Drawn over the splash so that text keeps its contrast regardless of the art
	/// behind it. This does the real legibility work; glyph outlines are insurance.
	///
	/// Both ramps start at the frame's opening rather than the window edge: the frame
	/// covers the outer FrameThickness pixels, so a ramp anchored to the window would
	/// hide its strongest end underneath the border and leave the title row washed out.
	constexpr Rect TopScrim{ 0, FrameThickness, WindowWidth, 200 };
	constexpr Rect BottomScrim{ 0, 300, WindowWidth, WindowHeight - FrameThickness };

	/// Applied to the frame's opening, not the window, so the darkening lands on the
	/// visible art instead of underneath the border.
	constexpr float VignetteStrength = 0.35f;

	// --- title bar ------------------------------------------------------------
	/// The drag strip covers the whole top of the window including the frame itself,
	/// so the border is grabbable and not just the text row. The buttons are carved
	/// out of it in LauncherView::HitTestCaption.
	constexpr Rect Caption{ 0, 0, WindowWidth, 88 };

	constexpr Rect TitleText{ 68, 48, 600, 88 };
	constexpr Rect VersionText{ 600, 60, 756, 80 };
	constexpr Rect MinimizeButton{ 768, 52, 800, 84 };
	constexpr Rect CloseButton{ 808, 52, 840, 84 };

	/// Titlebar icons are square and centered in their button.
	constexpr int32 TitleIconSize = 16;

	// --- bottom bar -----------------------------------------------------------
	/// 98 tall on purpose: that is exactly the source art's height, so at 100% DPI the
	/// panel needs no vertical resampling and only its center stretches horizontally.
	/// Sits inside the frame opening with a small margin below it.
	constexpr Rect BottomPanel{ 60, 392, 840, 490 };

	constexpr Rect StatusLabel{ 88, 404, 580, 426 };
	constexpr Rect ProgressTrack{ 88, 434, 580, 462 };
	constexpr Rect PercentText{ 480, 437, 577, 459 };
	constexpr Rect PlayButton{ 600, 408, 820, 474 };
}
