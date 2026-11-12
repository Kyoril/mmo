// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "rectangle_shape.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg::graph
{
	namespace
	{
		/// The rectangle's local x and z axes on the x/z plane (unit length), from the yaw of its rotation.
		struct RectangleFrame
		{
			float ax = 1.0f;
			float az = 0.0f;
			float zx = 0.0f;
			float zz = 1.0f;

			/// World x/z offset of the local offset (lx, lz).
			void ToWorld(const float lx, const float lz, float& outX, float& outZ) const
			{
				outX = lx * ax + lz * zx;
				outZ = lx * az + lz * zz;
			}
		};

		RectangleFrame GetFrame(const ControlInstance& inst)
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
			RectangleFrame frame;
			frame.ax = ax;
			frame.az = az;
			frame.zx = -az;
			frame.zz = ax;
			return frame;
		}

		/// Local signs (-1, 0, +1) of a handle on the x and z axes.
		void HandleSigns(const RectangleHandle handle, float& sx, float& sz)
		{
			sx = 0.0f;
			sz = 0.0f;
			switch (handle)
			{
			case RectangleHandle::SideNegX:
				sx = -1.0f;
				break;
			case RectangleHandle::SidePosX:
				sx = 1.0f;
				break;
			case RectangleHandle::SideNegZ:
				sz = -1.0f;
				break;
			case RectangleHandle::SidePosZ:
				sz = 1.0f;
				break;
			case RectangleHandle::CornerNegNeg:
				sx = -1.0f;
				sz = -1.0f;
				break;
			case RectangleHandle::CornerPosNeg:
				sx = 1.0f;
				sz = -1.0f;
				break;
			case RectangleHandle::CornerPosPos:
				sx = 1.0f;
				sz = 1.0f;
				break;
			case RectangleHandle::CornerNegPos:
				sx = -1.0f;
				sz = 1.0f;
				break;
			}
		}

		/// New half extent and centre offset along one local axis when the side with the given sign moves to the
		/// local cursor coordinate; the opposite side at -sign * halfExtent stays.
		void DragAxis(const float sign, const float halfExtent, const float cursor, float& outHalfExtent, float& outCentre)
		{
			const float fixedSide = -sign * halfExtent;
			outHalfExtent = std::max(MinRectangleHalfExtent, sign * (cursor - fixedSide) * 0.5f);
			outCentre = fixedSide + sign * outHalfExtent;
		}
	}

	std::array<SplinePoint, 4> RectangleCorners(const ControlInstance& inst)
	{
		const RectangleFrame frame = GetFrame(inst);
		const float hx = std::fabs(inst.halfExtents.x);
		const float hz = std::fabs(inst.halfExtents.z);

		std::array<SplinePoint, 4> corners;
		const float signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
		for (size_t i = 0; i < corners.size(); ++i)
		{
			float dx = 0.0f;
			float dz = 0.0f;
			frame.ToWorld(signs[i][0] * hx, signs[i][1] * hz, dx, dz);

			SplinePoint p;
			p.x = inst.position.x + dx;
			p.z = inst.position.z + dz;
			p.y = inst.position.y;
			corners[i] = p;
		}
		return corners;
	}

	Vector3 RectangleHandlePosition(const ControlInstance& inst, const RectangleHandle handle)
	{
		const RectangleFrame frame = GetFrame(inst);
		float sx = 0.0f;
		float sz = 0.0f;
		HandleSigns(handle, sx, sz);

		float dx = 0.0f;
		float dz = 0.0f;
		frame.ToWorld(sx * std::fabs(inst.halfExtents.x), sz * std::fabs(inst.halfExtents.z), dx, dz);
		return Vector3(inst.position.x + dx, inst.position.y, inst.position.z + dz);
	}

	void DragRectangleHandle(ControlInstance& inst, const RectangleHandle handle, const float worldX, const float worldZ)
	{
		if (!std::isfinite(worldX) || !std::isfinite(worldZ))
		{
			return;
		}

		const RectangleFrame frame = GetFrame(inst);
		float sx = 0.0f;
		float sz = 0.0f;
		HandleSigns(handle, sx, sz);

		// Cursor in the rectangle's local frame.
		const float dx = worldX - inst.position.x;
		const float dz = worldZ - inst.position.z;
		const float lx = dx * frame.ax + dz * frame.az;
		const float lz = dx * frame.zx + dz * frame.zz;

		float hx = std::fabs(inst.halfExtents.x);
		float hz = std::fabs(inst.halfExtents.z);
		float cx = 0.0f;
		float cz = 0.0f;
		if (sx != 0.0f)
		{
			DragAxis(sx, hx, lx, hx, cx);
		}
		if (sz != 0.0f)
		{
			DragAxis(sz, hz, lz, hz, cz);
		}

		float offsetX = 0.0f;
		float offsetZ = 0.0f;
		frame.ToWorld(cx, cz, offsetX, offsetZ);
		const float newX = inst.position.x + offsetX;
		const float newZ = inst.position.z + offsetZ;
		if (!std::isfinite(newX) || !std::isfinite(newZ) || !std::isfinite(hx) || !std::isfinite(hz))
		{
			return;
		}

		inst.position.x = newX;
		inst.position.z = newZ;
		inst.halfExtents.x = hx;
		inst.halfExtents.z = hz;
	}
}
