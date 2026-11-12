// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "evaluator.h"
#include "graph.h"

#include "math/path_spline.h"
#include "pcg/placement.h"

#include <vector>

namespace mmo::pcg::graph
{
	/// @brief Points emitted by spawner nodes on their "Spawns" pin, converted to placements and clipped to the region.
	struct CollectedPlacements
	{
		/// @brief The placements, sorted by key.
		std::vector<Placement> placements;

		/// @brief Key collisions resolved, empty assets skipped, ...
		std::vector<String> warnings;

		/// @brief Points dropped for lying outside the region.
		size_t clipped = 0;
	};

	/// @brief Collects every node's "Spawns" output (Points items with a String column "asset").
	///        "Spawns" pins are terminal: their items are collected exactly as the node emits them, so linking a
	///        "Spawns" pin onward does not post-process what is placed (downstream results are not collected
	///        unless a downstream node emits its own "Spawns"). Each node whose "Spawns" pin is linked onward
	///        gets one warning.
	///        Item attributes: "kind" (String "Entity" | "Foliage" | "WorldModel"; an item with another or no
	///        kind is skipped with a warning), "collides" (Bool, default true). Position, rotation and scale
	///        come from the point.
	///
	///        Key = Hash(nodeId, item.sourceId, point.seed); a repeated key is re-hashed Hash(key, n) with n =
	///        1, 2, ... until it is unique, with one warning. Nodes are visited by ascending id, then items and
	///        points in order, so a key only depends on the points that precede an identical one.
	///
	///        Only points inside the closed region spline (x/z) are kept, using the containment of the
	///        samplers (RowIndexedPolygon, even-odd rule on the region evaluated with EvaluationStep): the
	///        lower edges (minimum x and z of the region) count as inside, the upper edges (maximum x and z) as
	///        outside, so neighbouring regions sharing an edge do not both keep a point on it. A region that is
	///        not a closed spline of at least 3 points keeps nothing (warning); all points count as clipped.
	CollectedPlacements CollectPlacements(const Graph& graph, const EvalResult& result, const PathSpline& region);

	/// @brief Deterministic JSON dump of an evaluation (per node: type, ms, fromCache, warnings, error, per pin:
	///        item kinds, tags, point count, first 16 points with attributes). Used by --pcg-eval-graph.
	/// @param includeTiming False leaves "ms" and "fromCache" out (they differ between otherwise identical runs).
	String WriteEvalDump(const Graph& graph, const EvalResult& result, bool includeTiming = true);
}
