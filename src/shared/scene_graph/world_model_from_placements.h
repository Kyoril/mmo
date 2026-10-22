// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/aabb.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	class WorldModel;

	/// @brief One placed mesh that is to become a mesh ref of a world model.
	struct WorldModelPlacementSource
	{
		/// @brief Path of the mesh asset (.hmsh).
		String meshPath;

		/// @brief World position of the placement.
		Vector3 position { 0.0f, 0.0f, 0.0f };

		/// @brief World rotation of the placement.
		Quaternion rotation { Quaternion::Identity };

		/// @brief Scale of the placement.
		Vector3 scale { 1.0f, 1.0f, 1.0f };

		/// @brief Material override of the mesh ref, empty for the mesh's own materials.
		String materialOverride;

		/// @brief Name of the mesh ref (editor display).
		String name;

		/// @brief Bounding box of the mesh in its own space.
		AABB meshBounds;
	};

	/// @brief Turns world placements into the groups of a world model whose origin is `origin`.
	/// @details Every placement becomes a mesh ref relative to the origin. Placements are sorted into
	///          exterior groups by the square grid cell of their position (cellSize world units, measured
	///          from the origin on the X/Z plane), so each group can be culled on its own. A group's bounding
	///          box covers its mesh refs' transformed mesh bounds; the model's box is recalculated. Groups
	///          are added in ascending cell order (Z, then X) and mesh refs keep the input order.
	/// @param model The world model that receives the groups (existing groups are kept).
	/// @param placements The placements to convert.
	/// @param origin World position that becomes the model's origin.
	/// @param cellSize Edge length of a grid cell; must be positive.
	/// @return Number of groups added.
	size_t AddWorldModelGroupsFromPlacements(WorldModel& model, const std::vector<WorldModelPlacementSource>& placements, const Vector3& origin, float cellSize);
}
