// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "placement.h"
#include "terrain_contribution.h"

#include <vector>

namespace mmo::pcg
{
	/// A placement a generator wrote into the world, with the id of the created object.
	struct OwnedPlacement
	{
		/// Stable placement key.
		uint64 key = 0;
		/// Id of the world object that was created for it.
		uint64 uniqueId = 0;
		/// Whether the object is a map entity (mesh or world model) or a foliage instance.
		PlacementKind kind = PlacementKind::Entity;
		/// The placement as it was written.
		Placement written;
	};

	/// Everything one generator instance currently has in the world (part of each generator's
	/// record): the terrain it wrote and the objects it created. The editor's apply core reads it to
	/// undo the previous build and replaces it with what the new build wrote.
	struct OwnedContent
	{
		/// Influence rect of the last build (meaningful while terrain is not empty).
		WorldRect influence{};
		/// Terrain samples the last build wrote, with the values that were there before.
		TerrainContribution terrain;
		/// Owned objects, sorted by key.
		std::vector<OwnedPlacement> placements;

		/// Returns true if the generator has nothing in the world.
		bool IsEmpty() const
		{
			return terrain.IsEmpty() && placements.empty();
		}
	};
}
