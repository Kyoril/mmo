// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Why a name cannot be the file name of a new asset, or empty if it can: no path separators, no "..",
	///        no characters that are invalid in file names, no trailing dot or space.
	/// @param name The name without extension.
	[[nodiscard]] String AssetFileNameError(const String& name);
}
