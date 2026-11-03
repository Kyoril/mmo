// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <vector>

namespace mmo::pcg
{
	/// What kind of scene object a placement becomes.
	enum class PlacementKind : uint8
	{
		/// A map entity (rocks, props).
		Entity,
		/// A foliage instance (trees, bushes).
		Foliage,
		/// A world model (WMO) instance, optionally dressed with prop kits.
		WorldModel
	};

	/// Replaces the material in one slot of an entity's mesh.
	struct PlacementMaterialOverride
	{
		/// Material slot index.
		uint8 materialIndex = 0;
		/// Material asset path.
		String materialName;

		bool operator==(const PlacementMaterialOverride& other) const
		{
			return materialIndex == other.materialIndex && materialName == other.materialName;
		}
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
		/// Material overrides (Entity only), sorted by material index.
		std::vector<PlacementMaterialOverride> materialOverrides;
		/// Prop kit paths (WorldModel only), in application order.
		std::vector<String> kitPaths;
	};
}
