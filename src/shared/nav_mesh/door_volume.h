// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/aabb.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"

namespace mmo::nav
{
	/// @brief How far a door's footprint is grown on each side, in world units. Barriers are
	/// single planes with zero thickness; growing by the agent radius gives the nav builder a
	/// strip wide enough to survive voxelization.
	constexpr float DoorVolumeInflate = 0.5f;

	/// @brief How far the door's Y range is grown up and down (the walkable climb), so floor
	/// spans slightly below the door's base still count as under the door.
	constexpr float DoorVolumeHeightPadding = 0.5f;

	/// @brief A door's footprint: an oriented box in the XZ plane plus a Y range.
	struct DoorVolume
	{
		/// World-space center of the box (y is ignored, see minY/maxY).
		Vector3 center;
		/// Unit box axes in the XZ plane (y = 0), perpendicular to each other.
		Vector3 axisX { Vector3::UnitX };
		Vector3 axisZ { Vector3::UnitZ };
		/// Half extents along axisX and axisZ, inflation included.
		float halfX = 0.f;
		float halfZ = 0.f;
		/// World Y range, padding included.
		float minY = 0.f;
		float maxY = 0.f;

		/// @brief Whether the XZ projection of a point lies inside the box.
		[[nodiscard]] bool ContainsXZ(const Vector3& point) const;

		/// @brief Axis-aligned world bounds of the box, Y range included.
		void GetWorldBounds(Vector3& min, Vector3& max) const;

		/// @brief The four XZ corners as Recast vertices (x, y, z), counter-clockwise seen from above, y = minY.
		void GetCorners(float (&verts)[12]) const;
	};

	/// @brief Builds a door's local to world transform, matching the client's display entity:
	/// position, uniform scale, and the spawn rotation followed by a fixed +90 degree yaw.
	/// @param position Spawn position.
	/// @param rotation Spawn rotation. A zero quaternion (unset in data) is treated as identity.
	/// @param scale Uniform scale; values <= 0 are treated as 1.
	[[nodiscard]] Matrix4 MakeDoorTransform(const Vector3& position, const Quaternion& rotation, float scale);

	/// @brief Computes the inflated XZ footprint of a door from its collision bounds.
	/// @param localBounds Bounds of the door mesh's collision tree, in mesh space.
	/// @param transform Door transform, see MakeDoorTransform.
	[[nodiscard]] DoorVolume ComputeDoorVolume(const AABB& localBounds, const Matrix4& transform);
}
