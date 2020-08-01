
#include "math_utils.h"

#include <cmath>


namespace mmo
{
	bool FloatEqual(float a, float b, float tolerance)
	{
		if (std::fabs(b - a) <= tolerance)
			return true;
		else
			return false;
	}
}
