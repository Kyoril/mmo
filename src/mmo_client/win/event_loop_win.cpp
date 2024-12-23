// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "event_loop.h"

#include "base/clock.h"
#include "graphics/graphics_device.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>

#include <thread>


namespace mmo
{
	// Event loop events
	signal<void(float deltaSeconds, GameTime timestamp)> EventLoop::Idle;
	signal<void()> EventLoop::Paint;
	signal<bool(int32, bool)> EventLoop::KeyDown;
	signal<bool(uint16)> EventLoop::KeyChar;
	signal<bool(int32)> EventLoop::KeyUp;
	signal<bool(EMouseButton, int32, int32)> EventLoop::MouseDown;
	signal<bool(EMouseButton, int32, int32)> EventLoop::MouseUp;
	signal<bool(int32, int32)> EventLoop::MouseMove;
	signal<bool(int32)> EventLoop::MouseWheel;

	static int s_captureCount = 0;

	void EventLoop::Initialize()
	{
	}

	void EventLoop::Destroy()
	{
	}

	// TODO: better way of handling
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
		// Process windows messages
		MSG msg = { nullptr };
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			// Check for WM_QUIT first and don't process it if it's there
			if (msg.message == WM_QUIT)
			{
				return false;
			}

			// Process input messages
			switch (msg.message)
			{
			case WM_KEYDOWN:
				{
					const bool wasKeyPreviouslyDown = (msg.lParam & (1 << 30)) != 0;
					KeyDown(static_cast<int32>(msg.wParam), wasKeyPreviouslyDown);
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

	void EventLoop::Run()
	{
		// Capture timestamp
		auto lastIdle = GetAsyncTimeMs();

		auto& gx = GraphicsDevice::Get();
		const auto gxWindow = gx.GetAutoCreatedWindow();

		// Run the event loop until the operating system tells us to stop here
		while (true)
		{
			// Process operating system input
			if (!ProcessOsInput())
				break;

			// Get current timestamp
			auto currentTime = GetAsyncTimeMs();
			auto timePassed = static_cast<float>((currentTime - lastIdle) / 1000.0);
			lastIdle = currentTime;

			// Raise idle event
			Idle(timePassed, currentTime);

			// Reset state for new frame
			gx.Reset();

			// Activate the main window as render target and clear it
			gxWindow->Activate();
			gxWindow->Clear(mmo::ClearFlags::All);

			// Paint contents
			Paint();

			// Flip buffers
			gxWindow->Update();
		}
	}

	void EventLoop::Terminate(int32 exitCode)
	{
		PostQuitMessage(exitCode);
	}
}