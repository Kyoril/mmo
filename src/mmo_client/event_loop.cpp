// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "event_loop.h"

#include "graphics/graphics_device.h"

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	include <windowsx.h>
#endif

#include <thread>
#include <chrono>


namespace mmo
{
	// Event loop events
	signal<void(float deltaSeconds, GameTime timestamp)> EventLoop::Idle;
	signal<void()> EventLoop::Paint;
	signal<bool(int32)> EventLoop::KeyDown;
	signal<bool(int32)> EventLoop::KeyUp;
	signal<bool(EMouseButton, int32, int32)> EventLoop::MouseDown;
	signal<bool(EMouseButton, int32, int32)> EventLoop::MouseUp;
	signal<bool(int32, int32)> EventLoop::MouseMove;


	void EventLoop::Initialize()
	{
	}

	void EventLoop::Destroy()
	{
	}

	bool EventLoop::ProcessOsInput()
	{
#ifdef _WIN32
		// Process windows messages
		MSG msg = { 0 };
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			// Process input messages
			switch (msg.message)
			{
			case WM_KEYDOWN:
				KeyDown(msg.wParam);
				break;
			case WM_KEYUP:
				KeyUp(msg.wParam);
				break;
			case WM_LBUTTONDOWN:
				MouseDown(MouseButton_Left, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_LBUTTONUP:
				MouseUp(MouseButton_Left, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_RBUTTONDOWN:
				MouseDown(MouseButton_Right, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_RBUTTONUP:
				MouseUp(MouseButton_Right, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_MBUTTONDOWN:
				MouseDown(MouseButton_Middle, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_MBUTTONUP:
				MouseUp(MouseButton_Middle, GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_MOUSEMOVE:
				MouseMove(GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
				break;
			case WM_MOUSEWHEEL:
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// If WM_QUIT is received, quit
		return (msg.message != WM_QUIT);
#else
		ASSERT(!"TODO: Implement os input handling on operating system!");
		return true;
#endif
	}

	void EventLoop::Run()
	{
		// Capture timestamp
		GameTime lastIdle = GetAsyncTimeMs();

		// Run the event loop until the operating system tells us to stop here
		while (true)
		{
			// Process operating system input
			if (!ProcessOsInput())
				break;

			// Get current timestamp
			GameTime currentTime = GetAsyncTimeMs();
			float timePassed = static_cast<float>((currentTime - lastIdle) / 1000.0);
			lastIdle = currentTime;

			// Raise idle event
			Idle(timePassed, currentTime);

			// Get graphics device object
			auto& gx = GraphicsDevice::Get();
			gx.Clear(ClearFlags::All);

			// Raise paint event
			Paint();

			// Flip buffers
			gx.Present();
		}
	}

	void EventLoop::Terminate(int32 exitCode)
	{
#ifdef _WIN32
		PostQuitMessage(exitCode);
#else
		ASSERT(!"TODO: Implement event loop termination on operating system!");
#endif
	}
}
