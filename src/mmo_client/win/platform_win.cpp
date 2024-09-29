// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "platform_win.h"
#include <Windows.h>

namespace mmo
{
	static bool s_mouseCaptured = false;
	static int s_mouseCursorHiddenCount = 0;
	static int s_mouseCaptureX = 0;
	static int s_mouseCaptureY = 0;

	void PlatformWin::CaptureMouse()
	{
		if (s_mouseCaptured) return;

		POINT pt;
		GetCursorPos(&pt);
		s_mouseCaptureX = pt.x;
		s_mouseCaptureY = pt.y;

		HideCursor();

		s_mouseCaptured = true;
	}

	void PlatformWin::ReleaseMouseCapture()
	{
		if (!s_mouseCaptured) return;

		SetCursorPos(s_mouseCaptureX, s_mouseCaptureY);
		ShowCursor();

		s_mouseCaptured = false;
	}

	void PlatformWin::ShowCursor()
	{
		if (--s_mouseCursorHiddenCount <= 0)
		{
			s_mouseCursorHiddenCount = 0;
			::ShowCursor(true);
		}
	}

	void PlatformWin::HideCursor()
	{
		if (++s_mouseCursorHiddenCount == 1)
		{
			::ShowCursor(false);
		}
	}
}
