// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#pragma once

#include "base/typedefs.h"
#include <array>

namespace mmo
{
	/// Console aliases for the existing, mutually hostile Human and Orc player templates.
	inline constexpr std::array<uint32, 2> DevFactionTemplates = { 0, 7 };
	inline constexpr std::array<const char*, 2> DevFactionNames = { "Alias 1", "Alias 2" };
}
