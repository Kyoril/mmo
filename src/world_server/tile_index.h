#pragma once

#include "base/typedefs.h"
#include "base/vector.h"

namespace mmo
{
	typedef int32 TileIndex;
	typedef Vector<TileIndex, 2> TileIndex2D;
	typedef Vector<TileIndex2D, 2> TileIndex2DPair;

	/// Determines whether a tile is inside a tile area.
	/// @param point The tile coordinates.
	/// @param rect The tile area. First coordinate is the top-left tile, second is the bottom-right tile.
	inline bool IsInside(
	    const TileIndex2D &point,
	    const TileIndex2DPair &rect)
	{
		const auto &topLeft = rect[0];
		const auto &bottomRight = rect[1];

		return
		    (point.x() >= topLeft.x()) &&
		    (point.y() >= topLeft.y()) &&
		    (point.x() < bottomRight.x()) &&
		    (point.y() < bottomRight.y());
	}

	/// Builds a tile area pair based of a center tile and a tile range.
	/// @param center The tile coordinate of the center.
	/// @param range The range in tile indices.
	inline TileIndex2DPair GetTileAreaAround(const TileIndex2D &center, TileIndex range)
	{
		const TileIndex2D topLeft(
		    center.x() - range,
		    center.y() - range);

		const TileIndex2D bottomRight(
		    center.x() + range + 1,
		    center.y() + range + 1);

		return {topLeft, bottomRight};
	}
}
