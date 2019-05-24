
#pragma once

#include <cassert>

#define ASSERT(x) assert(x)

#ifdef _DEBUG
#	define VERIFY(x) ASSERT(x)
#else
#	define VERIFY(x) (void)(x)
#endif


#ifdef _WIN32
#	define PLATFORM_WINDOWS 1
#endif
#ifdef __linux__
#	define PLATFORM_LINUX 1
#endif
#ifdef __APPLE__
#	define PLATFORM_APPLE 1
#endif

#ifndef PLATFORM_WINDOWS
#	define PLATFORM_WINDOWS 0
#endif
#ifndef PLATFORM_LINUX
#	define PLATFORM_LINUX 0
#endif
#ifndef PLATFORM_APPLE
#	define PLATFORM_APPLE 0
#endif
