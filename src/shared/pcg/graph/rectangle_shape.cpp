// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "rectangle_shape.h"

#include <cmath>

namespace mmo::pcg::graph
{
	std::array<SplinePoint, 4> RectangleCorners(const ControlInstance& inst)
	{
		// Yaw from the rotated local x axis projected onto x/z.
		const Vector3 axis = inst.rotation * Vector3(1, 0, 0);
		float ax = axis.x;
		float az = axis.z;
		const float len = std::sqrt(ax * ax + az * az);
		if (!(len > 1.0e-6f))
		{
			ax = 1.0f;
			az = 0.0f;
		}
		else
		{
			ax /= len;
			az /= len;
		}

		// Yaw maps local x to (c, -s) and local z to (s, c) = (-x.z, x.x).
		const float zx = -az;
		const float zz = ax;
		const float hx = std::fabs(inst.halfExtents.x);
		const float hz = std::fabs(inst.halfExtents.z);

		std::array<SplinePoint, 4> corners;
		const float signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
		for (size_t i = 0; i < corners.size(); ++i)
		{
			SplinePoint p;
			p.x = inst.position.x + signs[i][0] * hx * ax + signs[i][1] * hz * zx;
			p.z = inst.position.z + signs[i][0] * hx * az + signs[i][1] * hz * zz;
			p.y = inst.position.y;
			corners[i] = p;
		}
		return corners;
	}
}
