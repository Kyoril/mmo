// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	class Scene;
	class MovableObject;

	/// @brief Resolves the surface type id of the floor beneath a world position by casting
	///        a short downward ray against collidable scene objects (terrain tiles resolve
	///        their dominant splat layer, entities the hit submesh material).
	/// @param scene The scene to query.
	/// @param position The world position (typically a unit's feet position).
	/// @param ignore Optional movable to skip (e.g. the querying unit's own entity).
	/// @return The surface type id at the floor hit, or 0 when nothing resolved.
	uint32 ResolveFloorSurfaceType(Scene& scene, const Vector3& position, const MovableObject* ignore = nullptr);
}
