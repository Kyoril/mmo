// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "event_loop.h"

#include "base/clock.h"
#include "graphics/graphics_device.h"


namespace mmo
{
	// Static signal member definitions — defined here once for all platforms.
	signal<void(float deltaSeconds, GameTime timestamp)> EventLoop::Idle;
	signal<void()> EventLoop::Paint;
	signal<bool(int32, bool)> EventLoop::KeyDown;
	signal<bool(uint16)> EventLoop::KeyChar;
	signal<bool(int32)> EventLoop::KeyUp;
	signal<bool(EMouseButton, int32, int32)> EventLoop::MouseDown;
	signal<bool(EMouseButton, int32, int32)> EventLoop::MouseUp;
	signal<bool(int32, int32)> EventLoop::MouseMove;
	signal<bool(int32)> EventLoop::MouseWheel;

	void EventLoop::Initialize()
	{
	}

	void EventLoop::Destroy()
	{
	}

	void EventLoop::Run()
	{
		auto lastIdle = GetAsyncTimeMs();

		auto& gx = GraphicsDevice::Get();
		const auto gxWindow = gx.GetAutoCreatedWindow();

		while (true)
		{
			if (!ProcessOsInput())
			{
				break;
			}

			auto currentTime = GetAsyncTimeMs();
			auto timePassed = static_cast<float>((currentTime - lastIdle) / 1000.0);
			lastIdle = currentTime;

			Idle(timePassed, currentTime);

			gx.Reset();

			gxWindow->Activate();
			gxWindow->Clear(mmo::ClearFlags::All);

			Paint();

			gxWindow->Update();
		}
	}
}
