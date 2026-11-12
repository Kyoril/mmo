// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "owned_content.h"
#include "graph/graph_data.h"
#include "math/path_spline.h"
#include "terrain_io/water_flow.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <vector>

namespace mmo::pcg
{
	/// Everything persisted about one graph instance: a graph applied to an area of a world, plus the content it owns.
	struct GraphInstanceRecord
	{
		/// Stable id of the instance.
		uint64 id = 0;
		/// Display name.
		String name;
		/// Asset path of the graph, e.g. "Editor/Pcg/Biomes/FalwynForest/Graphs/Forest.hpcgg".
		String graphPath;
		/// Closed loop on the XZ plane; heights are unused.
		PathSpline area;
		/// Stable id per area point; must be parallel to area.points.
		std::vector<uint64> pointIds;
		/// Next id handed out to a new area point. Reading raises it to max(pointIds) + 1 if it is lower.
		uint64 nextPointId = 1;
		/// Seed handed to the graph evaluation.
		uint64 seed = 1;
		/// Graph parameter values that replace the graph's defaults.
		graph::AttributeSet parameterOverrides;
		/// Maximum number of points a graph node may produce.
		size_t pointLimit = 2'000'000;
		/// Width (m) inside the area border over which terrain writes fade to nothing (0 = hard edge). Finite, >= 0.
		float borderFade = 8.0f;
		/// Hash of the seam set (the area edges shared with touching graph instances) of the last build, 0 = none. Stored in
		/// the JSON blob under "seamHash" (only when not 0; a missing key reads as 0).
		uint64 seamHash = 0;
		/// What the last build put into the world.
		OwnedContent content;
		/// River paths written by the last build, in op order. They take part in the world's river flow.
		std::vector<terrain_io::water_flow::FlowPath> flowPaths;
	};

	/// Writes the record as binary (magic PCGG, version 2): id, a JSON text blob (name, graphPath, seed,
	/// parameterOverrides, pointLimit, borderFade), the area points, pointIds, nextPointId, the owned content, then
	/// the flow paths (path count, then per path a point count and x, z, halfWidth floats per point).
	/// Deterministic. A record without flow paths still writes version 2.
	/// @return False if pointIds is not parallel to area.points.
	bool WriteGraphInstanceRecord(io::Writer& writer, const GraphInstanceRecord& record);

	/// Reads a record written by WriteGraphInstanceRecord. Unknown JSON keys are ignored; a blob without borderFade
	/// (written before it existed) reads the default 8. The area always reads as a closed Catmull-Rom loop.
	/// Version 1 (before flow paths) reads with no flow paths.
	/// @return False on a bad magic, unsupported version or truncated/corrupt data; out is then unspecified.
	bool ReadGraphInstanceRecord(io::Reader& reader, GraphInstanceRecord& out);

	/// Whether two closed XZ areas share interior; touching along an edge or at a vertex is not overlap. Areas are
	/// sampled the way the graph nodes sample them. Areas with fewer than 3 points never overlap.
	bool AreasOverlap(const PathSpline& a, const PathSpline& b);
}
