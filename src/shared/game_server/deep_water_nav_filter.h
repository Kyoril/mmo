// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_server/water_path.h"

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

namespace mmo
{
	/// @brief Whether any corner of a navigation polygon lies under water deep enough to swim in.
	/// @param tileVerts The vertices of the navigation mesh tile the polygon belongs to
	///	(dtMeshTile::verts). A null pointer reports no water.
	/// @param poly The polygon to test.
	/// @param waterQuery Water surface query. An empty query reports no water anywhere.
	[[nodiscard]] bool IsPolyInDeepWater(const float* tileVerts, const dtPoly& poly, const WaterSurfaceQuery& waterQuery);

	/// @brief Navigation query filter which rejects polygons lying under water deep enough to
	///	swim in, on top of what another filter decides.
	///
	///	The navigation mesh is baked on the ground, so as far as Detour is concerned the bottom of
	///	a lake is ordinary walkable ground. A unit which is not able to swim has to treat water
	///	that deep like a wall, so this filter carves it out of the mesh and its paths are routed
	///	around the water body. Water shallow enough to stand in stays passable: the unit wades
	///	through it.
	///
	///	Not thread safe, like the water data it queries: owned and used by the world thread.
	class DeepWaterQueryFilter final : public dtQueryFilter
	{
	public:
		/// @param base Filter which decides first, usually the world instance's door filter. Has
		///	to outlive this filter.
		/// @param waterQuery Water surface query. An empty query leaves every polygon passable.
		DeepWaterQueryFilter(const dtQueryFilter& base, WaterSurfaceQuery waterQuery);

		/// @copydoc dtQueryFilter::passFilter
		bool passFilter(dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const override;

	private:
		const dtQueryFilter& m_base;
		WaterSurfaceQuery m_waterQuery;
	};
}
