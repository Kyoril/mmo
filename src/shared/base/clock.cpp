// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "clock.h"
#if defined(WIN32) || defined(_WIN32)
#	include <Windows.h>
#	include <mmsystem.h>
#	pragma comment(lib, "winmm.lib")
#else
#	include <sys/time.h>
#	include <unistd.h>
#	include <time.h>
#	ifdef __MACH__
#		include <mach/clock.h>
#		include <mach/mach.h>
#	endif
#endif

namespace mmo
{
	GameTime GetAsyncTimeMs()
	{
#if defined(WIN32) || defined(_WIN32)
		static double s_qpcScaleToMs = 0.0;

		static bool s_qpcExistsTested = false;
		static bool s_qpcExists = false;

		LARGE_INTEGER timestamp;
		if (!s_qpcExistsTested)
		{
			if (QueryPerformanceFrequency(&timestamp))
			{
				s_qpcScaleToMs = 1000.0 / static_cast<double>(timestamp.QuadPart);
				s_qpcExists = true;
			}

			s_qpcExistsTested = true;
		}

		if (s_qpcExists)
		{
			QueryPerformanceCounter(&timestamp);
			return static_cast<GameTime>(static_cast<double>(timestamp.QuadPart) * s_qpcScaleToMs);
		}

		return static_cast<GameTime>(::GetTickCount64());
#elif defined(__APPLE__)
        struct timeval tp;
        gettimeofday(&tp, nullptr);
        return uint32(
            (tp.tv_sec * 1000) + (tp.tv_usec / 1000) % uint64(0x00000000FFFFFFFF));
#else
		// Use a monotonic clock if available
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		uint64_t ms = (uint64_t)ts.tv_sec * 1000ULL + (ts.tv_nsec / 1000000ULL);
		return static_cast<GameTime>(ms);
#endif
    }
}
