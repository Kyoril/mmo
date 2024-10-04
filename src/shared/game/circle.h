
#pragma once
#include "base/vector.h"

namespace mmo
{
	typedef float Distance;
	typedef Vector<Distance, 2> Point;

	/// Represents any 2d shape in the game world.
	struct IShape
	{
		virtual ~IShape() = default;

		virtual Vector<Point, 2> GetBoundingRect() const = 0;

		virtual bool IsPointInside(const Point& point) const = 0;
	};

	/// Represents a circle shape in the game world.
	class Circle : public IShape
	{
	public:

		Distance x, y;
		Distance radius;

		Circle();

		explicit Circle(Distance x, Distance y, Distance radius);

		virtual ~Circle() = default;

		virtual Vector<Point, 2> GetBoundingRect() const override;

		virtual bool IsPointInside(const Point& point) const override;
	};
}
