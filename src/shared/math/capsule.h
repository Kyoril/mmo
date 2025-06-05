#pragma once

#include "vector3.h"

namespace mmo
{
	struct Capsule
	{
		Vector3 pointA;
		Vector3 pointB;
		float radius = 0.0f;
	};
}
