// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "prop_kit.h"
#include "world_model.h"

#include "base/typedefs.h"
#include "math/aabb.h"

#include <functional>
#include <optional>
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
}
