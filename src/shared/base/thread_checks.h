// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "macros.h"

namespace mmo
{
	/// @brief Returns true if called on the main thread.
	///
	/// The main thread is the one that called TaskSystem::Initialize(). Processes that never
	/// initialize the TaskSystem (servers, tools) always get true, keeping the checks inert there.
	/// Implemented in task_system.cpp; declared here so widely included headers (signal.h)
	/// can assert thread affinity without pulling in the full task system.
	bool IsMainThread();

	/// @brief Asserts that the current thread is the main thread (Debug only, inert until
	///        TaskSystem::Initialize was called). Used to enforce main-thread-only contracts
	///        on signals, GraphicsDevice and resource managers.
#define ASSERT_MAIN_THREAD() ASSERT(::mmo::IsMainThread())
}
