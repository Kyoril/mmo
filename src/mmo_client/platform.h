
#pragma once

#include "base/macros.h"

#if PLATFORM_WINDOWS
#	include "win/platform_win.h"
typedef mmo::PlatformWin Platform;
#else
#	error Please add a platform typedef for your target compile platform
#endif