// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	class GraphicsDevice;
}

namespace mmo::test
{
	/// @brief Creates the suite's null graphics device on first use and returns it thereafter.
	///
	/// The device is a process-wide singleton and asserts if a second one is created, so it has
	/// to be owned in one place. A function-local static per translation unit looks like it does
	/// that but does not: each unit gets its own static and the second unit to run trips the
	/// assert, which is a failure that only appears once two test files in the suite both need a
	/// device.
	GraphicsDevice& EnsureNullDevice();
}
