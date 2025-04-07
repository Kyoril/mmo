// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file macros.h
 *
 * @brief Defines common macros used throughout the codebase.
 *
 * This file contains various utility macros for assertions, error handling,
 * platform detection, and development helpers. These macros provide consistent
 * ways to handle common programming patterns across the codebase.
 */

#pragma once

#include <cassert>

/**
 * @def ASSERT(x)
 * @brief Wrapper around the standard assert macro.
 * @param x The condition to check. If false, triggers an assertion failure.
 */
#ifndef ASSERT
#	define ASSERT(x) assert(x)
#endif

/**
 * @def VERIFY(x)
 * @brief Verifies a condition in debug mode, but executes it without checking in release mode.
 * @param x The condition to verify.
 * 
 * In debug builds, this behaves like ASSERT. In release builds, the expression is
 * still evaluated but the result is discarded.
 */
#ifdef _DEBUG
#	define VERIFY(x) ASSERT(x)
#else
#	define VERIFY(x) (void)(x)
#endif

/**
 * @def FATAL(x, msg)
 * @brief Asserts a condition with an error message. If the condition is false, triggers an assertion.
 * @param x The condition to check.
 * @param msg The error message to display if the condition is false.
 * 
 * In debug builds, this triggers an assertion with the provided message.
 * In release builds, this currently behaves like ASSERT but will be updated to terminate
 * the application with a fatal error message.
 */
#ifdef _DEBUG
#	define FATAL(x, msg) ASSERT(x && msg)
#else
// TODO: Change this later on to terminate the application with a fatal error message etc.
#	define FATAL(x, msg) ASSERT(x)
#endif

/**
 * @def UNREACHABLE()
 * @brief Marks a code path that should never be executed.
 * 
 * If this macro is reached during execution, it will trigger an assertion failure.
 * This is useful for marking impossible code paths in switch statements or after
 * exhaustive condition handling.
 */
#define UNREACHABLE() ASSERT(! "This code path should not be reached!")

/**
 * @def TODO(msg)
 * @brief Marks code that needs to be implemented.
 * @param msg A message describing what needs to be done.
 * 
 * This macro triggers an assertion failure with a TODO message, making it
 * impossible to run the program without implementing the required functionality.
 */
#define TODO(msg) ASSERT(! "TODO: " # msg)

/**
 * @name Platform Detection Macros
 * @{
 * 
 * These macros are used to detect and define the current platform.
 * They are set to 1 for the current platform and 0 for all other platforms.
 * This allows for platform-specific code to be conditionally compiled.
 */

/**
 * @def PLATFORM_WINDOWS
 * @brief Set to 1 when compiling for Windows platforms, 0 otherwise.
 */
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
/** @} */
