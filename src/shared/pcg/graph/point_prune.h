// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph_data.h"

#include <vector>

namespace mmo::pcg::graph
{
	// Seeding contract of the samplers (what a prune over their output can rely on):
	// - SampleArea and SampleTerrainSurface seed a point from its world cell and the node seed only, so where
	//   areas overlap (or one area is linked twice) the same node produces exact duplicates: same position,
	//   same seed. Different nodes (different node seeds) never share seeds.
	// - SampleSpline seeds from the 1 cm position cell, the arc distance and an item key (sourceId, or the input
	//   index for generated items with sourceId 0), so coincident, derived or self-intersecting splines yield
	//   distinct seeds; only the same control linked twice repeats its points.
	// - PruneByDistance resolves exact duplicates (distance 0 conflicts for any minSpacing > 0) by its rank:
	//   priority, then seed, then index; for equal seeds the lower index, i.e. the earlier item, wins.

	/// @brief Options of PruneByDistance.
	struct PruneOptions
	{
		/// @brief Points closer than this to a kept point are removed. Zero or less (or non-finite) keeps everything.
		float minSpacing = 0.0f;

		/// @brief Measure distances on the x/z plane (true) or in 3D (false).
		bool planar = true;

		/// @brief Number of local-maximum rounds before the final pass (see PruneByDistance). More rounds keep
		///        more points but widen the radius a point's fate depends on: (2 * rounds + 1) * minSpacing.
		uint32 rounds = 1;
	};

	/// @brief Deterministic, local minimum-distance prune (shared by Sample Area and Self Prune).
	///
	///        Points rank by priority (higher first), then by seed (higher first), then by index (lower first).
	///        Two points conflict when their distance is below minSpacing. Each round keeps every undecided point
	///        that outranks all its undecided conflicting neighbours and removes the undecided neighbours of the
	///        points kept in that round. A final pass then keeps an undecided point only if it has no kept
	///        neighbour and outranks all its undecided neighbours. No two kept points conflict.
	///
	///        Locality: whether a point is kept depends only on points within (2 * rounds + 1) * minSpacing of it,
	///        so changing the input in one place leaves the result unchanged farther away. The result does not
	///        depend on the order of the points except through the final index tie-break.
	///        Points with a non-finite position never conflict.
	/// @param points The points.
	/// @param options Spacing, metric and rounds.
	/// @param priorities Optional, one value per point (higher is kept first); null ranks by seed alone.
	///        Ignored if its size differs from the number of points.
	/// @return A keep mask (1 = keep) with one entry per point, suitable for PointSet::Keep.
	std::vector<uint8> PruneByDistance(const std::vector<Point>& points, const PruneOptions& options, const std::vector<float>* priorities = nullptr);
}
