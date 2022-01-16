#pragma once

#include <algorithm>

namespace mmo
{
	template <typename T>
	static T Clamp(const T val, const  T minval, const  T maxval)
	{
		return std::max(std::min(val, maxval), minval);
	}
}