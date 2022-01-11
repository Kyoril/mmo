// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <cassert>

#ifndef ASSERT
#	define ASSERT(x) assert(x)
#endif

#ifdef _DEBUG
#	define VERIFY(x) ASSERT(x)
#else
#	define VERIFY(x) (void)(x)
#endif

#ifdef _DEBUG
#	define FATAL(x, msg) ASSERT(x && msg)
#else
// TODO: Change this later on to terminate the application with a fatal error message etc.
#	define FATAL(x, msg) ASSERT(x)
#endif

#define UNREACHABLE() ASSERT(! "This code path should not be reached!")

#define TODO(msg) ASSERT(! "TODO: " # msg)

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
