// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "event_loop.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>

#include <unordered_set>


namespace mmo
{
	static int s_captureCount = 0;

	// Virtual keys for which KeyDown was fired. Windows doesn't deliver a WM_KEYUP to
	// this queue when the release happens while another window has focus (Alt+Tab,
	// Win+key, task switch) — reconciling this set against the physical key state each
	// frame lets us synthesize the missed release instead of leaving the key stuck down.
	static std::unordered_set<int32> s_reportedKeysDown;

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
					s_reportedKeysDown.insert(virtualKey);
					KeyDown(virtualKey, wasKeyPreviouslyDown);
					break;
				}
			case WM_CHAR:
				KeyChar(static_cast<uint16>(msg.wParam));
				break;
			case WM_KEYUP:
			// Releasing a key while Alt is held arrives as WM_SYSKEYUP — without this
			// case a Ctrl release during an Alt combo would leave Ctrl stuck down.
			case WM_SYSKEYUP:
				{
					const int32 virtualKey = static_cast<int32>(msg.wParam);
					s_reportedKeysDown.erase(virtualKey);
					KeyUp(virtualKey);
					break;
				}
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

		// Self-heal stuck keys: if the OS no longer reports a tracked key as held, its
		// release never reached this queue — synthesize the missed KeyUp now. In the
		// rare race where the real WM_KEYUP is still in flight, consumers see a second
		// KeyUp, which is harmless (state removal is idempotent).
		for (auto it = s_reportedKeysDown.begin(); it != s_reportedKeysDown.end();)
		{
			if ((::GetAsyncKeyState(*it) & 0x8000) == 0)
			{
				const int32 virtualKey = *it;
				it = s_reportedKeysDown.erase(it);
				KeyUp(virtualKey);
			}
			else
			{
				++it;
			}
		}

		return true;
	}

	void EventLoop::Terminate(int32 exitCode)
	{
		PostQuitMessage(exitCode);
	}
}
