// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "door_volume.h"

#include "math/degree.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo::nav
{
	bool DoorVolume::ContainsXZ(const Vector3& point) const
	{
		const Vector3 offset(point.x - center.x, 0.f, point.z - center.z);
		return std::abs(offset.Dot(axisX)) <= halfX && std::abs(offset.Dot(axisZ)) <= halfZ;
	}

	void DoorVolume::GetWorldBounds(Vector3& min, Vector3& max) const
	{
		const float extentX = std::abs(axisX.x) * halfX + std::abs(axisZ.x) * halfZ;
		const float extentZ = std::abs(axisX.z) * halfX + std::abs(axisZ.z) * halfZ;
		min = Vector3(center.x - extentX, minY, center.z - extentZ);
		max = Vector3(center.x + extentX, maxY, center.z + extentZ);
	}

	void DoorVolume::GetCorners(float (&verts)[12]) const
	{
		const Vector3 ax = axisX * halfX;
		const Vector3 az = axisZ * halfZ;
		const Vector3 corners[4] = { center - ax - az, center - ax + az, center + ax + az, center + ax - az };
		for (int i = 0; i < 4; ++i)
		{
			verts[i * 3 + 0] = corners[i].x;
			verts[i * 3 + 1] = minY;
			verts[i * 3 + 2] = corners[i].z;
		}
	}

	Matrix4 MakeDoorTransform(const Vector3& position, const Quaternion& rotation, float scale)
	{
		if (scale <= 0.f)
		{
			scale = 1.f;
		}

		Quaternion spawnRotation = rotation;
		if (spawnRotation.w == 0.f && spawnRotation.x == 0.f && spawnRotation.y == 0.f && spawnRotation.z == 0.f)
		{
			spawnRotation = Quaternion::Identity;
		}

		// The client hangs the display entity under a child node with a fixed +90 degree yaw
		// (GameObjectC::SetupSceneObjects); the server's LOS collision mirrors it.
		const Quaternion entityYawOffset(Degree(90), Vector3::UnitY);

		Matrix4 transform;
		transform.MakeTransform(position, Vector3(scale, scale, scale), spawnRotation * entityYawOffset);
		return transform;
	}

	DoorVolume ComputeDoorVolume(const AABB& localBounds, const Matrix4& transform)
	{
		const Vector3& lo = localBounds.min;
		const Vector3& hi = localBounds.max;
		const Vector3 localCorners[8] = {
			{ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { lo.x, hi.y, lo.z }, { hi.x, hi.y, lo.z },
			{ lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { lo.x, hi.y, hi.z }, { hi.x, hi.y, hi.z },
		};

		// Box axes: the door's local X axis projected onto the ground plane, and its perpendicular.
		// A door is upright, so its local X never points straight up. If it does, fall back to world axes.
		const Vector3 origin = transform * Vector3::Zero;
		Vector3 axisX = transform * Vector3::UnitX - origin;
		axisX.y = 0.f;
		if (axisX.GetSquaredLength() < 1.0e-8f)
		{
			axisX = Vector3::UnitX;
		}
		axisX.Normalize();
		const Vector3 axisZ(-axisX.z, 0.f, axisX.x);

		float minX = std::numeric_limits<float>::max(), maxX = -std::numeric_limits<float>::max();
		float minZ = minX, maxZ = maxX;
		float minY = minX, maxY = maxX;
		for (const Vector3& local : localCorners)
		{
			const Vector3 world = transform * local;
			const Vector3 flat(world.x, 0.f, world.z);
			minX = std::min(minX, flat.Dot(axisX));
			maxX = std::max(maxX, flat.Dot(axisX));
			minZ = std::min(minZ, flat.Dot(axisZ));
			maxZ = std::max(maxZ, flat.Dot(axisZ));
			minY = std::min(minY, world.y);
			maxY = std::max(maxY, world.y);
		}

		DoorVolume volume;
		volume.axisX = axisX;
		volume.axisZ = axisZ;
		volume.center = axisX * ((minX + maxX) * 0.5f) + axisZ * ((minZ + maxZ) * 0.5f);
		volume.halfX = (maxX - minX) * 0.5f + DoorVolumeInflate;
		volume.halfZ = (maxZ - minZ) * 0.5f + DoorVolumeInflate;
		volume.minY = minY - DoorVolumeHeightPadding;
		volume.maxY = maxY + DoorVolumeHeightPadding;
		return volume;
	}
}
