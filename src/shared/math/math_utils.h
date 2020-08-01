
#include <limits>


namespace mmo
{
	bool FloatEqual(float a, float b, float tolerance = std::numeric_limits<float>::epsilon());
}