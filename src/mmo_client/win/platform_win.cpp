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
		::GetCursorPos(&pt);
		s_mouseCaptureX = pt.x;
		s_mouseCaptureY = pt.y;

		HideCursor();

		s_mouseCaptured = true;
	}

	void PlatformWin::ReleaseMouseCapture()
	{
		if (!s_mouseCaptured) return;

		::SetCursorPos(s_mouseCaptureX, s_mouseCaptureY);
		ShowCursor();

		s_mouseCaptured = false;
	}

	void PlatformWin::ResetCursorPosition()
	{
		SetCursorPos(s_mouseCaptureX, s_mouseCaptureY);
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

	bool PlatformWin::IsMouseCaptured()
	{
		return s_mouseCaptured;
	}

	void PlatformWin::GetCapturedMousePosition(int& x, int& y)
	{
		if (!s_mouseCaptured) {
			x = -1;
			y = -1;
			return;
		}
		x = s_mouseCaptureX;
		y = s_mouseCaptureY;
	}

	void PlatformWin::GetCursorPos(int& x, int& y)
	{
		POINT pt;
		::GetCursorPos(&pt);
		x = pt.x;
		y = pt.y;
	}

	void PlatformWin::SetCursorPos(int x, int y)
	{
		::SetCursorPos(x, y);
	}

	bool PlatformWin::IsShiftKeyDown()
	{
		return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	}
}
