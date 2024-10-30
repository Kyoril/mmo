// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <cmath>
#include <limits>
#include "constants.h"


namespace mmo
{
    class Vector3;
    class Matrix4;
	class Quaternion;
	
	bool FloatEqual(float a, float b, float tolerance = std::numeric_limits<float>::epsilon());

    static float DegreesToRadians(const float degrees) { return degrees * Deg2Rad; }
    static float RadiansToDegrees(const float radians) { return radians * Rad2Deg; }


    Matrix4 MakeViewMatrix(const Vector3& position, const Quaternion& orientation);

	template<class T>
	T Interpolate(T min, T max, float t)
	{
		// Some optimization
		if (t <= 0.0f)
		{
			return min;
		}
		else if (t >= 1.0f)
		{
			return max;
		}

		// Linear interpolation
		return min + (max - min) * t;
	}
}
