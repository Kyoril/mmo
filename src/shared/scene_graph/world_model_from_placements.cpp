// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_from_placements.h"

#include "world_model.h"
#include "math/matrix4.h"

#include <cmath>
#include <map>

namespace mmo
{
	size_t AddWorldModelGroupsFromPlacements(WorldModel& model, const std::vector<WorldModelPlacementSource>& placements, const Vector3& origin, const float cellSize)
	{
		ASSERT(cellSize > 0.0f);

		// Cell key (z, x) keeps the group order stable: rows from north to south, west to east within a row.
		std::map<std::pair<int32, int32>, std::vector<const WorldModelPlacementSource*>> cells;
		for (const WorldModelPlacementSource& placement : placements)
		{
			const Vector3 local = placement.position - origin;
			const int32 cellX = static_cast<int32>(std::floor(local.x / cellSize));
			const int32 cellZ = static_cast<int32>(std::floor(local.z / cellSize));
			cells[{ cellZ, cellX }].push_back(&placement);
		}

		for (const auto& [cell, members] : cells)
		{
			WorldModelGroup& group = model.AddGroup();
			group.SetName("Cell " + std::to_string(cell.second) + "," + std::to_string(cell.first));
			group.SetFlags(WorldModelGroupFlags::Exterior);

			AABB bounds;
			bounds.SetNull();
			for (const WorldModelPlacementSource* placement : members)
			{
				WorldModelMeshRef ref;
				ref.meshPath = placement->meshPath;
				ref.position = placement->position - origin;
				ref.rotation = placement->rotation;
				ref.scale = placement->scale;
				ref.materialOverride = placement->materialOverride;
				ref.name = placement->name;
				group.AddMeshRef(ref);

				if (!placement->meshBounds.IsNull())
				{
					Matrix4 transform;
					transform.MakeTransform(ref.position, ref.scale, ref.rotation);
					AABB meshBounds = placement->meshBounds;
					meshBounds.Transform(transform);
					bounds.Combine(meshBounds);
				}
			}

			group.SetBoundingBox(bounds);
		}

		model.RecalculateBoundingBox();
		return cells.size();
	}
}
