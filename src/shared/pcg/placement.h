// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/quaternion.h"
#include "math/vector3.h"

namespace mmo::pcg
{
	/// What kind of scene object a placement becomes.
	enum class PlacementKind : uint8
	{
		/// A map entity (rocks, props).
		Entity,
		/// A foliage instance (trees, bushes).
		Foliage
	};

	/// One generated object. The key is stable across re-generation so the editor can diff.
	struct Placement
	{
		/// Stable key: derived from the seed, the rule and the world anchored cell or arc step.
		uint64 key = 0;
		PlacementKind kind = PlacementKind::Entity;
		String asset;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale = Vector3(1.0f, 1.0f, 1.0f);
		bool collides = true;
	};
}
