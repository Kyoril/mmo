// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "clock.h"
#if defined(WIN32) || defined(_WIN32)
#	include <Windows.h>
#	include <mmsystem.h>
#	pragma comment(lib, "winmm.lib")
#else
#	include <sys/time.h>
#	include <unistd.h>
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
#else
        struct timeval tp;
        gettimeofday(&tp, nullptr);
        return uint32(
            (tp.tv_sec * 1000) + (tp.tv_usec / 1000) % uint64(0x00000000FFFFFFFF));
#endif
    }
}
