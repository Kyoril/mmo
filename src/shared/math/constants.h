#pragma once

#include <cmath>
#include <limits>

namespace mmo
{
    static const float PosInfinity = std::numeric_limits<float>::infinity();
    static const float NegInfinity = -std::numeric_limits<float>::infinity();
    static const float Pi = static_cast<float>(4.0 * atan(1.0));
    static const float TwoPi = static_cast<float>(2.0 * Pi);
    static const float HalfPi = static_cast<float>(0.5 * Pi);
	static const float Deg2Rad = Pi / static_cast<float>(180.0);
	static const float Rad2Deg = static_cast<float>(180.0) / Pi;
	static const float Log2 = std::log(static_cast<float>(2.0));
}
