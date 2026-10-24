// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "oasis.h"
#include "placement.h"
#include "terrain_contribution.h"
#include "math/path_spline.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <vector>

namespace mmo::pcg
{
	/// A placement the oasis wrote into the world, with the id of the created object.
	struct OwnedPlacement
	{
		/// Stable placement key.
		uint64 key = 0;
		/// Id of the world object that was created for it.
		uint64 uniqueId = 0;
		/// Whether the object is an entity or a foliage instance.
		PlacementKind kind = PlacementKind::Entity;
		/// The placement as it was written.
		Placement written;
	};

	/// Everything persisted about one oasis instance.
	struct OasisRecord
	{
		uint64 id = 0;
		String name;
		uint64 seed = 1;
		OasisParams params;
		/// Closed, Catmull-Rom.
		PathSpline loop;
		/// Influence rect of the last build.
		WorldRect influence{};
		TerrainContribution terrain;
		/// Sorted by key.
		std::vector<OwnedPlacement> placements;
	};

	/// Writes the record as chunked binary (magic PCGO, version 1). Deterministic: the same record
	/// produces the same bytes.
	/// @return True on success.
	bool WriteOasisRecord(io::Writer& writer, const OasisRecord& record);

	/// Reads a record written by WriteOasisRecord.
	/// @return False on a bad magic, unsupported version or truncated/corrupt data; out is then unspecified.
	bool ReadOasisRecord(io::Reader& reader, OasisRecord& out);
}
