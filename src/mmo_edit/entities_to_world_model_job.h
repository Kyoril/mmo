// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Runs the unattended "entities to world model" job described by a JSON file.
	/// @details Collects the mesh entities of a world's page rect whose mesh path matches the job's
	///          include filters (and none of its exclude filters) and whose placed bounding radius reaches
	///          minRadius, writes them as one world model (exterior grid cell groups, see
	///          AddWorldModelGroupsFromPlacements), places that world model at the origin and deletes the
	///          converted entity files. Static meshes do not show on baked minimaps; world models do.
	///
	///          Job keys: "world" (e.g. "Worlds/Development/Development"), "pages" [x0, z0, x1, z1],
	///          "include" / "exclude" (mesh path substrings), "minRadius" (default 0), "output" (.hwmo path),
	///          "origin" [x, y, z] (default: centre of the converted placements' positions), "cellSize"
	///          (default 64), "name" and "category" of the placement, "dryRun" (only log what would happen).
	/// @param jsonPath Path of the job description on disk.
	/// @return false if the job could not be read, a file could not be loaded or written, or nothing matched.
	bool ApplyEntitiesToWorldModelJob(const String& jsonPath);
}
