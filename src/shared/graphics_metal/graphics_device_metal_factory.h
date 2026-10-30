// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>

namespace mmo
{
	class GraphicsDevice;

	/// @brief Creates the Metal graphics device. Declared apart from the device class so plain C++
	///        code can create one without seeing the Objective-C++ types the device is built from.
	std::unique_ptr<GraphicsDevice> CreateGraphicsDeviceMetal();
}
