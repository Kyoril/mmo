// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "updraft_placement.h"

namespace mmo
{
	UpdraftEmitterPlacement GetUpdraftEmitterPlacement(const UpdraftVolume& volume)
	{
		UpdraftEmitterPlacement placement;
		if (volume.radius > 0.0f)
		{
			placement.base = volume.center - Vector3(0.0f, volume.radius, 0.0f);
			placement.halfWidth = volume.radius * UpdraftSphereFootprint;
			placement.halfDepth = volume.radius * UpdraftSphereFootprint;
			placement.height = volume.radius * 2.0f;
			return placement;
		}

		placement.base = volume.center - Vector3(0.0f, volume.halfExtents.y, 0.0f);
		placement.halfWidth = volume.halfExtents.x;
		placement.halfDepth = volume.halfExtents.z;
		placement.yaw = volume.orientation;
		placement.height = volume.halfExtents.y * 2.0f;
		return placement;
	}
}
