// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "random.h"

#include <ctime>


namespace mmo
{
	// Initialize the random generator used in this project
	RandomnessGenerator RandomGenerator(time(nullptr));
}
