// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph_data.h"

#include "base/typedefs.h"
#include "math/vector3.h"

#include <cstddef>
#include <vector>

namespace mmo::pcg::graph
{
	/// @brief One coloured line segment of a node's debug drawing, in world space.
	struct DebugLine
	{
		Vector3 a;
		Vector3 b;

		/// @brief Colour as argb.
		uint32 argb = 0xFFFFFFFF;
	};

	/// @brief The debug drawing of a collection.
	struct DebugGeometry
	{
		/// @brief The line segments.
		std::vector<DebugLine> lines;

		/// @brief Number of points in the collection's point items.
		size_t pointsTotal = 0;

		/// @brief Number of those points that were drawn (as a box or a tick).
		size_t pointsDrawn = 0;

		/// @brief Number of drawn points shown as boxes.
		size_t boxesDrawn = 0;

		/// @brief Number of lines the collection produces under the box and point limits, without the line limit.
		size_t linesTotal = 0;

		/// @brief Number of lines drawn (lines.size(), at most the line limit).
		size_t linesDrawn = 0;
	};

	/// @brief Height of the vertical tick drawn for a point beyond the box limit.
	constexpr float DebugTickHeight = 1.0f;

	/// @brief Builds the debug drawing of a collection. Points become oriented boxes from their rotation, scale and
	///        bounds (12 lines each); beyond `boxLimit` drawn points, each further point becomes a short vertical tick
	///        (DebugTickHeight). When the collection holds more than `pointLimit` points, only every k-th point (in
	///        collection order, k = ceil(total / pointLimit)) is drawn, recorded in pointsDrawn. Splines and areas
	///        become polylines evaluated at EvaluationStep(spline, SamplingStep), closed for closed splines. Params
	///        items draw nothing. The colour is `argb` with its brightness scaled by the point's density (density 1
	///        keeps `argb`, density 0 keeps 30 % of it); lines of splines use `argb`. At most `lineLimit` lines are
	///        produced: items are drawn in collection order until one does not fit (a point's box or tick is all or
	///        nothing, a polyline is cut at the limit), after which nothing more is drawn but linesTotal keeps
	///        counting. Deterministic.
	/// @param items The collection to draw.
	/// @param argb The base colour.
	/// @param boxLimit Number of sampled points shown as boxes; further points are ticks.
	/// @param pointLimit Number of points drawn at most (0 draws no points).
	/// @param lineLimit Number of lines produced at most, for points and polylines together.
	DebugGeometry BuildDebugGeometry(const Collection& items, uint32 argb, size_t boxLimit = 20'000, size_t pointLimit = 200'000,
		size_t lineLimit = 500'000);
}
