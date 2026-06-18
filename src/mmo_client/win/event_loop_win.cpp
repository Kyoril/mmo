// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "event_loop.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>


namespace mmo
{
	static int s_captureCount = 0;

	// Win32 mouse capture reference-counting — tracks button presses so that mouse
	// events keep arriving even when the cursor leaves the window during a drag.
	void IncreaseCapture(HWND wnd)
	{
		if (s_captureCount++ == 0)
		{
			::SetCapture(wnd);
		}
	}

	void DecreaseCapture()
	{
		if (--s_captureCount == 0)
		{
			::ReleaseCapture();
		}

		if (s_captureCount < 0) s_captureCount = 0;
	}

	bool EventLoop::ProcessOsInput()
	{
		MSG msg = { nullptr };
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				return false;
			}

			switch (msg.message)
			{
			case WM_KEYDOWN:
				{
					const bool wasKeyPreviouslyDown = (msg.lParam & (1 << 30)) != 0;
					const int32 virtualKey = static_cast<int32>(msg.wParam);
					KeyDown(virtualKey, wasKeyPreviouslyDown);
					break;
				}
			case WM_CHAR:
				KeyChar(static_cast<uint16>(msg.wParam));
				break;
			case WM_KEYUP:
				KeyUp(static_cast<int32>(msg.wParam));
				break;
			case WM_LBUTTONDOWN:
				IncreaseCapture(msg.hwnd);
				MouseDown(MouseButton_Left, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_LBUTTONUP:
				DecreaseCapture();
				MouseUp(MouseButton_Left, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_RBUTTONDOWN:
				IncreaseCapture(msg.hwnd);
				MouseDown(MouseButton_Right, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_RBUTTONUP:
				DecreaseCapture();
				MouseUp(MouseButton_Right, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_MBUTTONDOWN:
				IncreaseCapture(msg.hwnd);
				MouseDown(MouseButton_Middle, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_MBUTTONUP:
				DecreaseCapture();
				MouseUp(MouseButton_Middle, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_MOUSEMOVE:
				MouseMove(GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_MOUSEWHEEL:
				MouseWheel(static_cast<int16>(HIWORD(msg.wParam)) / WHEEL_DELTA);
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return true;
	}

	void EventLoop::Terminate(int32 exitCode)
	{
		PostQuitMessage(exitCode);
	}
}
