// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "door_marking.h"

#include "common.h"

#include "Recast.h"

namespace mmo
{
	namespace
	{
		/// Area id Recast writes under a door. Not a valid flag combination of its own: it is
		/// replaced by the span's original area plus the door bit right after marking.
		constexpr unsigned char TemporaryDoorArea = 0xFE;
	}

	void MarkDoorAreas(rcContext& ctx, rcCompactHeightfield& chf, const std::vector<nav::DoorVolume>& doors)
	{
		if (doors.empty())
		{
			return;
		}

		const int spanCount = chf.spanCount;
		std::vector<unsigned char> original(chf.areas, chf.areas + spanCount);

		for (const nav::DoorVolume& door : doors)
		{
			Vector3 min, max;
			door.GetWorldBounds(min, max);
			if (max.x < chf.bmin[0] || min.x > chf.bmax[0] || max.z < chf.bmin[2] || min.z > chf.bmax[2])
			{
				continue;
			}

			float verts[12];
			door.GetCorners(verts);
			rcMarkConvexPolyArea(&ctx, verts, 4, door.minY, door.maxY, TemporaryDoorArea, chf);
		}

		for (int i = 0; i < spanCount; ++i)
		{
			if (chf.areas[i] != TemporaryDoorArea)
			{
				continue;
			}

			// Unwalkable spans stay unwalkable; a door only marks floor that exists.
			chf.areas[i] = original[i] == RC_NULL_AREA
				? RC_NULL_AREA
				: static_cast<unsigned char>(original[i] | poly_flags::Door);
		}
	}
}
