// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "prop_kit.h"
#include "world_model.h"

#include "base/typedefs.h"
#include "math/aabb.h"

#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace mmo
{
	/// @brief A prop of a kit that blocks (or nearly blocks) a portal of a world model.
	struct PortalClearanceIssue
	{
		/// @brief Name of the offending prop (falls back to its mesh path if it has no name).
		String propName;

		/// @brief Index of the portal in WorldModel::GetPortals().
		int32 portalIndex = 0;

		/// @brief Gap between the prop's collision bounds and the portal bounds (0 if they overlap).
		float distance = 0.0f;
	};

	/// @brief Finds props of a kit whose collision bounds come closer to a portal than the margin.
	///        Props are in world model space, like the portals. The distance is approximated as the
	///        gap between the prop's world-space AABB and the portal's AABB inflated by the margin.
	/// @param model The world model providing the portals.
	/// @param kit The kit whose props are checked.
	/// @param collisionBoundsOf Returns the local collision bounds of a mesh, or nullopt if the mesh
	///        has no collision (such props are ignored).
	/// @param margin Required clearance around a portal in meters.
	/// @return One issue per prop/portal pair that violates the clearance.
	std::vector<PortalClearanceIssue> FindPropsNearPortals(const WorldModel& model, const PropKit& kit,
		const std::function<std::optional<AABB>(const String& meshPath)>& collisionBoundsOf, float margin = 0.6f);

	/// @brief Outcome of RemoveMeshRefsByName.
	struct MeshRefRemoval
	{
		/// @brief Number of mesh refs removed over all groups.
		size_t removed = 0;

		/// @brief Requested names that matched no mesh ref, in sorted order.
		std::vector<String> unmatchedNames;
	};

	/// @brief Removes every mesh ref of every group whose name is in the given set, as the
	///        default-kit migration does when it moves a building's furniture into a kit.
	/// @param model The world model to edit in place.
	/// @param names Exact mesh ref names to remove.
	/// @return How many refs were removed and which names matched nothing.
	MeshRefRemoval RemoveMeshRefsByName(WorldModel& model, const std::set<String>& names);
}
