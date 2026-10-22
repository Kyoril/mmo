// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/aabb.h"
#include "math/quaternion.h"
#include "math/vector3.h"

namespace mmo
{
	/// Transforms a box from a child's space into its parent's space (scale, then rotation, then
	/// translation, like a scene node) and returns the box enclosing the result.
	/// @param bounds The box in the child's space. A null box stays null.
	[[nodiscard]] AABB TransformPlacementBounds(const AABB& bounds, const Vector3& position, const Quaternion& rotation, const Vector3& scale);

	/// Grows a parent's bounds by a child's bounds placed in the parent's space, so a world model's
	/// bounds also cover its child world models (a model's own bounds only cover its own groups).
	/// @param parent The parent's bounds so far, in the parent's space. May be null.
	/// @param childBounds The child's bounds in the child's own space. A null box adds nothing.
	/// @return The combined bounds.
	[[nodiscard]] AABB CombineChildBounds(const AABB& parent, const AABB& childBounds, const Vector3& position, const Quaternion& rotation, const Vector3& scale);
}
