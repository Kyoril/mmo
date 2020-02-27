// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include <random>
#include <limits>


namespace mmo
{
	typedef std::mt19937 RandomnessGenerator;


	/// The random number generator instance.
	extern RandomnessGenerator RandomGenerator;
}
