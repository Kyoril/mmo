// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cmath>
#include <cstdlib>

namespace mmo
{
	/// @brief Parses a float from text: the whole text must be one finite number (std::strtof syntax).
	/// @param text The text to parse.
	/// @param out Receives the value (also on failure, then unspecified).
	/// @return True if the text is a finite number with nothing after it.
	inline bool ParseFloatText(const String& text, float& out)
	{
		char* end = nullptr;
		out = std::strtof(text.c_str(), &end);
		return !text.empty() && end == text.c_str() + text.size() && std::isfinite(out);
	}
}
