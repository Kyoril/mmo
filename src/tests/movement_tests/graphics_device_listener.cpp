// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Catch2 keeps the reporter and listener interfaces out of the default header; without
// this the TestEventListenerBase hooks are declared but their argument types are not.
#define CATCH_CONFIG_EXTERNAL_INTERFACES
#include "catch.hpp"

#include "graphics/graphics_device.h"

namespace mmo
{
	/// Brings up the null graphics device for the duration of the run. Scene's constructor
	/// calls GraphicsDevice::Get(), so a device has to exist before the first test that
	/// builds a scene -- which is every test in this suite.
	///
	/// This is a Catch listener rather than a custom main() so the suite can share the
	/// catch_main library with every other suite; the run-start and run-end hooks give the
	/// same ordering guarantees a hand-written main() did.
	struct NullGraphicsDeviceListener final : Catch::TestEventListenerBase
	{
		using TestEventListenerBase::TestEventListenerBase;

		void testRunStarting(Catch::TestRunInfo const&) override
		{
			GraphicsDevice::CreateNull(GraphicsDeviceDesc{});
		}

		void testRunEnded(Catch::TestRunStats const&) override
		{
			GraphicsDevice::Destroy();
		}
	};
}

// CATCH_REGISTER_LISTENER pastes the type name into an identifier, so it cannot be given a
// namespace-qualified name. The alias keeps the listener itself inside mmo.
using NullGraphicsDeviceListener = mmo::NullGraphicsDeviceListener;
CATCH_REGISTER_LISTENER(NullGraphicsDeviceListener)
