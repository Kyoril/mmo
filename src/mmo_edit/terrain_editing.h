#pragma once
#include "base/box.h"
#include "base/vector.h"
#include "paging/page.h"

namespace mmo
{
	namespace terrain
	{
		namespace editing
		{
			typedef Vector<size_t, 2> VertexPosition;
			typedef Box<VertexPosition> VertexRectangle;

			struct GlobalVertexPosition
			{
				Vector<size_t, 2> asVector;

				explicit GlobalVertexPosition();
				explicit GlobalVertexPosition(const Vector<size_t, 2>& vector);
			};

			struct PageLocalVertexPosition
			{
				Vector<size_t, 2> asVector;

				explicit PageLocalVertexPosition();
				explicit PageLocalVertexPosition(const Vector<size_t, 2>& vector);
			};

			struct GlobalPixelPosition
			{
				Vector<size_t, 2> asVector;

				explicit GlobalPixelPosition();
				explicit GlobalPixelPosition(const Vector<size_t, 2>& vector);
			};

			struct PageLocalPixelPosition
			{
				Vector<size_t, 2> asVector;

				explicit PageLocalPixelPosition();
				explicit PageLocalPixelPosition(const Vector<size_t, 2>& vector);
			};

			GlobalVertexPosition globalize(const PagePosition& page, const PageLocalVertexPosition& relative);
			std::pair<PageLocalVertexPosition, PagePosition> localize(const GlobalVertexPosition& global);

			GlobalPixelPosition globalize(const PagePosition& page, const PageLocalPixelPosition& relative);
			std::pair<PageLocalPixelPosition, PagePosition> localize(const GlobalPixelPosition& global);

			struct BrushDimensions
			{
				typedef size_t Radius;

				Radius innerRadius;
				Radius outerRadius;

				explicit BrushDimensions(Radius inner, Radius outer);
			};


			template <class IntensityHandler, class UnitType>
			void iterateBrushIntensities(const UnitType& where, const BrushDimensions& brush, const IntensityHandler& handler)
			{
				const size_t x = where.asVector[0] - brush.outerRadius;
				const size_t y = where.asVector[1] - brush.outerRadius;

				for (size_t vertX = x; vertX < x + brush.outerRadius * 2; vertX++)
				{
					for (size_t vertY = y; vertY < y + brush.outerRadius * 2; vertY++)
					{
						float factor = 1.0f;
						float dist = sqrtf(
							powf(static_cast<float>(brush.outerRadius) - vertX + x, 2.0f) +
							powf(static_cast<float>(brush.outerRadius) - vertY + y, 2.0f)
						);

						if (dist > brush.innerRadius)
						{
							dist -= brush.innerRadius;
							factor =
								std::min(
									std::max(1.0f - (dist / static_cast<float>(brush.outerRadius - brush.innerRadius)), 0.0f),
									1.0f
								);
						}

						UnitType pos;
						pos.asVector = makeVector(vertX, vertY);
						handler(pos, factor);
					}
				}
			}

		}
	}
}
