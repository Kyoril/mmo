
#pragma once

#include <limits>
#include <cmath>


namespace mmo
{
    class Vector3;
    class Matrix4;
	class Quaternion;
	
	static const float PosInfinity = std::numeric_limits<float>::infinity();
    static const float NegInfinity = -std::numeric_limits<float>::infinity();
    static const float Pi = static_cast<float>(4.0f * ::atanf(1.0f));
    static const float TwoPi = 2.0f * Pi;
    static const float HalfPi = 0.5f * Pi;
    static const float Deg2Rad = Pi / 180.0f;
    static const float Rad2Deg = 180.0f / Pi;

	bool FloatEqual(float a, float b, float tolerance = std::numeric_limits<float>::epsilon());

    static float DegreesToRadians(float degrees) { return degrees * Deg2Rad; }
    static float RadiansToDegrees(float radians) { return radians * Rad2Deg; }


    Matrix4 MakeViewMatrix(const Vector3& position, const Quaternion& orientation);
}
