// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <random>
#include <limits>


namespace mmo
{
	typedef std::mt19937 RandomnessGenerator;


	/// The random number generator instance.
	extern RandomnessGenerator RandomGenerator;
}
