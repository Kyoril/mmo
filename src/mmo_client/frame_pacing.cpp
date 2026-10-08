// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "frame_pacing.h"

#include "console/console_var.h"

#include "base/signal.h"
#include "graphics/display_settings.h"
#include "graphics/render_window.h"

#include <algorithm>
#include <thread>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#endif

namespace mmo
{
	namespace
	{
		ConsoleVar* s_maxFpsEnabledVar = nullptr;
		ConsoleVar* s_maxFpsVar = nullptr;
		ConsoleVar* s_maxFpsBkEnabledVar = nullptr;
		ConsoleVar* s_maxFpsBkVar = nullptr;
		ConsoleVar* s_targetFpsEnabledVar = nullptr;
		ConsoleVar* s_targetFpsVar = nullptr;

		scoped_connection_container s_connections;

		FramePacer s_pacer;
		DynamicResolutionController s_dynamicResolution;

		/// Limits below this would make the game unusable rather than save power.
		constexpr float MinFpsLimit = 8.0f;

		float ReadLimit(const ConsoleVar* enabled, const ConsoleVar* value)
		{
			if (!enabled || !value || !enabled->GetBoolValue())
			{
				return 0.0f;
			}

			return std::max(MinFpsLimit, value->GetFloatValue());
		}

		void ApplyTargetFps()
		{
			s_dynamicResolution.SetTargetFps(ReadLimit(s_targetFpsEnabledVar, s_targetFpsVar));
		}

		/// Waits for the given point in time as precisely as the platform allows, without spinning a
		/// whole core for the duration: limiting the frame rate is meant to save power.
		void WaitUntil(const FramePacer::Clock::time_point deadline)
		{
			using namespace std::chrono;

#ifdef _WIN32
			// A high resolution waitable timer is accurate to well below a millisecond (Windows 10 1803
			// and later), while Sleep() rounds up to the 15.6 ms scheduler tick.
			static const HANDLE s_timer = []()
			{
				HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
				return timer ? timer : CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
			}();

			while (true)
			{
				const auto remaining = deadline - FramePacer::Clock::now();
				if (remaining <= microseconds(200))
				{
					break;
				}

				// Leave a small margin to the deadline and spin the rest, waking up late costs a frame.
				LARGE_INTEGER due;
				due.QuadPart = -static_cast<LONGLONG>(duration_cast<nanoseconds>(remaining - microseconds(500)).count() / 100);
				if (!s_timer || due.QuadPart >= 0 || !SetWaitableTimer(s_timer, &due, 0, nullptr, nullptr, FALSE))
				{
					break;
				}

				WaitForSingleObject(s_timer, INFINITE);
			}
#else
			if (deadline - FramePacer::Clock::now() > milliseconds(1))
			{
				std::this_thread::sleep_until(deadline - milliseconds(1));
			}
#endif

			while (FramePacer::Clock::now() < deadline)
			{
				std::this_thread::yield();
			}
		}
	}

	void FramePacing::Initialize()
	{
		s_maxFpsEnabledVar = ConsoleVarMgr::RegisterConsoleVar("gxMaxFpsEnabled", "Limit the frame rate while the game window is in the foreground. 1 = on, 0 = unlimited.", "0");
		s_maxFpsVar = ConsoleVarMgr::RegisterConsoleVar("gxMaxFps", "Highest frame rate while the game window is in the foreground, if gxMaxFpsEnabled is on.", "144");
		s_maxFpsBkEnabledVar = ConsoleVarMgr::RegisterConsoleVar("gxMaxFpsBkEnabled", "Limit the frame rate while the game window is in the background or minimized. 1 = on, 0 = unlimited.", "1");
		s_maxFpsBkVar = ConsoleVarMgr::RegisterConsoleVar("gxMaxFpsBk", "Highest frame rate while the game window is in the background or minimized, if gxMaxFpsBkEnabled is on.", "30");
		s_targetFpsEnabledVar = ConsoleVarMgr::RegisterConsoleVar("gxTargetFpsEnabled", "Lower the 3D render resolution while the frame rate stays below gxTargetFps. 1 = on, 0 = off.", "0");
		s_targetFpsVar = ConsoleVarMgr::RegisterConsoleVar("gxTargetFps", "Frame rate the dynamic render resolution tries to hold, if gxTargetFpsEnabled is on.", "60");

		s_connections += s_targetFpsEnabledVar->Changed.connect([](ConsoleVar&, const std::string&) { ApplyTargetFps(); });
		s_connections += s_targetFpsVar->Changed.connect([](ConsoleVar&, const std::string&) { ApplyTargetFps(); });
		ApplyTargetFps();
	}

	void FramePacing::Destroy()
	{
		s_connections.disconnect();
		s_maxFpsEnabledVar = nullptr;
		s_maxFpsVar = nullptr;
		s_maxFpsBkEnabledVar = nullptr;
		s_maxFpsBkVar = nullptr;
		s_targetFpsEnabledVar = nullptr;
		s_targetFpsVar = nullptr;
	}

	bool FramePacing::NeedsGpuTiming()
	{
		return s_dynamicResolution.GetTargetFps() > 0.0f;
	}

	void FramePacing::EndFrame(const RenderWindow& window, const float frameSeconds, const double gpuMilliseconds)
	{
		const bool focused = window.HasFocus();

		// Background frames are throttled on purpose, judging the GPU on them would only add noise.
		if (focused)
		{
			s_dynamicResolution.Update(frameSeconds, gpuMilliseconds);
		}

		float limit = ReadLimit(s_maxFpsEnabledVar, s_maxFpsVar);
		if (!focused)
		{
			if (const float backgroundLimit = ReadLimit(s_maxFpsBkEnabledVar, s_maxFpsBkVar); backgroundLimit > 0.0f)
			{
				limit = limit > 0.0f ? std::min(limit, backgroundLimit) : backgroundLimit;
			}
		}

		const auto now = FramePacer::Clock::now();
		const auto deadline = s_pacer.NextFrameStart(now, limit);
		if (deadline > now)
		{
			WaitUntil(deadline);
		}
	}

	float FramePacing::GetDynamicRenderScale()
	{
		return s_dynamicResolution.GetScale();
	}
}
