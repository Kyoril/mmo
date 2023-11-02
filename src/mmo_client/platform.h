
#pragma once

#include "base/macros.h"

#if PLATFORM_WINDOWS
#	include "win/platform_win.h"
typedef mmo::PlatformWin Platform;
#elif PLATFORM_APPLE
#   include "macos/platform_osx.h"
typedef mmo::PlatformOsX Platform;
#else
#	error Please add a platform typedef for your target compile platform
#endif
