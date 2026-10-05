// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "geometry.h"
#include "base/typedefs.h"

namespace mmo::layout
{
	/// Shared logical dimensions, scaled for the display DPI.
	constexpr int32 WindowWidth = 1140;
	constexpr int32 WindowHeight = 740;
	constexpr int32 FrameThickness = 32;
	constexpr Rect WindowFrame{0, 0, WindowWidth, WindowHeight};
	constexpr Rect Content{32, 32, 1108, 708};
	constexpr Rect TitleBarPlate{32, 32, 1108, 100};
	constexpr Rect TitleBarEdge{32, 98, 1108, 100};
	constexpr Rect TitleBarShadow{32, 100, 1108, 108};
	constexpr Rect Caption{0, 0, WindowWidth, 100};
	constexpr Rect TitleText{66, 38, 374, 96};
	constexpr Rect VersionText{906, 62, 990, 84};
	constexpr Rect MinimizeButton{996, 54, 1030, 88};
	constexpr Rect CloseButton{1040, 54, 1074, 88};
	constexpr int32 TitleIconSize = 16;
	constexpr Rect Page{64, 112, 1076, 578};
	constexpr Rect HeroFrame{64, 112, 716, 406};
	constexpr Rect HeroImage{72, 120, 708, 398};
	constexpr Rect HeroScrim{72, 276, 708, 398};
	constexpr Rect HeroTitle{92, 332, 700, 368};
	constexpr Rect HeroSubtitle{94, 368, 692, 390};
	constexpr float SplashAnchorX = 0.5f;
	constexpr float SplashAnchorY = 0.45f;
	constexpr float VignetteStrength = 0.15f;
	constexpr Rect BottomPanel{60, 592, 1080, 690};
	constexpr Rect StatusLabel{88, 608, 846, 632};
	constexpr Rect ProgressTrack{88, 640, 626, 665};
	constexpr Rect PercentText{542, 640, 622, 665};
	constexpr Rect NoticeLabel{644, 638, 846, 676};
	constexpr Rect PlayButton{858, 608, 1058, 674};
}
