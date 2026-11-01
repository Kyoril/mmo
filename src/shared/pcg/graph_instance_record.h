// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "owned_content.h"
#include "graph/graph_data.h"
#include "math/path_spline.h"
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
		/// What the last build put into the world.
		OwnedContent content;
	};

	/// Writes the record as binary (magic PCGG, version 1): id, a JSON text blob (name, graphPath, seed,
	/// parameterOverrides, pointLimit), the area points, pointIds, nextPointId, then the owned content. Deterministic.
	/// @return False if pointIds is not parallel to area.points.
	bool WriteGraphInstanceRecord(io::Writer& writer, const GraphInstanceRecord& record);

	/// Reads a record written by WriteGraphInstanceRecord. Unknown JSON keys are ignored. The area always reads as a
	/// closed Catmull-Rom loop.
	/// @return False on a bad magic, unsupported version or truncated/corrupt data; out is then unspecified.
	bool ReadGraphInstanceRecord(io::Reader& reader, GraphInstanceRecord& out);

	/// Whether two closed XZ areas share interior; touching along an edge or at a vertex is not overlap. Areas are
	/// sampled the way the graph nodes sample them. Areas with fewer than 3 points never overlap.
	bool AreasOverlap(const PathSpline& a, const PathSpline& b);
}
