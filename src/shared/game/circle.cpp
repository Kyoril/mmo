
#include "circle.h"

namespace mmo
{
	Circle::Circle()
		: x(0.0f)
		, y(0.0f)
		, radius(0.0f)
	{
	}

	Circle::Circle(Distance x, Distance y, Distance radius)
		: x(x)
		, y(y)
		, radius(radius)
	{
	}

	Vector<Point, 2> Circle::GetBoundingRect() const
	{
		return {
			Point(x - radius, y - radius),
			Point(x + radius, y + radius)
		};
	}

	bool Circle::IsPointInside(const Point& point) const
	{
		const auto distSq = (Point(x, y) - point).lengthSq();
		const auto radiusSq = (radius * radius);
		return (distSq < radiusSq);
	}

}
