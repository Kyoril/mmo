// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "event_loop.h"

#include "base/clock.h"
#include "base/profiler.h"
#include "graphics/graphics_device.h"
#include "log/default_log.h"


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

			// Emit log entries buffered by worker/streaming threads (the log signal itself
			// is main-thread-only, see Log::Emit).
			g_DefaultLog.FlushBuffered();

			// The frame is bracketed here rather than inside a game state, so the profiler (and the
			// perf overlay) works in every state, not only while in the world.
			Profiler& profiler = Profiler::GetInstance();
			const bool profiling = profiler.IsEnabled();
			if (profiling)
			{
				profiler.BeginFrame();

				// Counters of the frame that was just presented. The batch count is latched by the
				// render window right before Present, the GPU time resolves a few frames late.
				profiler.SetCounter("Batches", static_cast<double>(gx.GetBatchCount()));
				if (const double gpuMs = gx.GetLastFrameGpuTimeMs(); gpuMs >= 0.0)
				{
					profiler.SetCounter("GPU frame (ms)", gpuMs);
				}

				gx.BeginFrameGpuTimer();
			}

			Idle(timePassed, currentTime);

			gx.Reset();

			gxWindow->Activate();
			gxWindow->Clear(mmo::ClearFlags::All);

			Paint();

			if (profiling)
			{
				gx.EndFrameGpuTimer();
				profiler.EndFrame();
			}

			{
				// Attributed to the next frame, as EndFrame already ran. Present blocks when the GPU
				// is behind (or on VSync), so this is the clearest "GPU-bound" signal on the CPU side.
				PROFILE_SCOPE("Present");
				gxWindow->Update();
			}
		}
	}
}
