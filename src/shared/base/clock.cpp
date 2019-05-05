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
	GameTime getCurrentTime()
	{
#if defined(WIN32) || defined(_WIN32)
		return ::GetTickCount64();
#else
        struct timeval tp;
        gettimeofday(&tp, nullptr);
        return UInt32(
            (tp.tv_sec * 1000) + (tp.tv_usec / 1000) % UInt64(0x00000000FFFFFFFF));
#endif
    }
}
