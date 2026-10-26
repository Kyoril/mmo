// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "deep_water_nav_filter.h"

#include <utility>

namespace mmo
{
	bool IsPolyInDeepWater(const float* tileVerts, const dtPoly& poly, const WaterSurfaceQuery& waterQuery)
	{
		if (!waterQuery || tileVerts == nullptr)
		{
			return false;
		}

		// Any corner in deep water rejects the whole polygon: a unit which can not swim keeps
		// clear of the water body instead of walking onto the part of a polygon that dips into it.
		for (uint8 i = 0; i < poly.vertCount; ++i)
		{
			const float* vertex = &tileVerts[poly.verts[i] * 3];
			if (IsSwimmableWaterAt(Vector3(vertex[0], vertex[1], vertex[2]), waterQuery))
			{
				return true;
			}
		}

		return false;
	}

	DeepWaterQueryFilter::DeepWaterQueryFilter(const dtQueryFilter& base, WaterSurfaceQuery waterQuery)
		: m_base(base)
		, m_waterQuery(std::move(waterQuery))
	{
	}

	bool DeepWaterQueryFilter::passFilter(const dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const
	{
		if (!m_base.passFilter(ref, tile, poly))
		{
			return false;
		}

		if (tile == nullptr || poly == nullptr)
		{
			return true;
		}

		return !IsPolyInDeepWater(tile->verts, *poly, m_waterQuery);
	}
}
