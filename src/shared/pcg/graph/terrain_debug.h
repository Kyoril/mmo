// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "debug_geometry.h"
#include "terrain_compose.h"

#include "base/typedefs.h"

#include <cstddef>
#include <functional>

namespace mmo::pcg::graph
{
	/// @brief Height above the composed ground at which op outlines and section copy rects are drawn.
	constexpr float TerrainDebugOutlineLift = 0.25f;

	/// @brief Colour (argb, blue) of a river's water surface line.
	constexpr uint32 TerrainDebugRiverSurfaceArgb = 0xFF3399FFu;

	/// @brief Colour (argb, brown) of a river's bed edges.
	constexpr uint32 TerrainDebugRiverBedArgb = 0xFF8B5A2Bu;

	/// @brief Builds the debug drawing of prepared terrain ops on a base terrain.
	/// @details First the outline of every op (its spline or area, see PreparedTerrain::GetOutlines; SectionPath ops
	///          also their copy rects, edges split into pieces of at most `step`), drawn in argbRaise
	///          TerrainDebugOutlineLift above the composed ground; a River op instead draws its water surface line at
	///          the surface height (+ the lift) in TerrainDebugRiverSurfaceArgb and its two bed edges on the composed
	///          ground in TerrainDebugRiverBedArgb. Then height ticks: on the world-anchored grid of
	///          multiples of `step` inside the op influence (PreparedTerrain::GetInfluence), row by row (z, then x),
	///          one vertical line per sample whose composed height differs from the base, from the base height to
	///          the composed height, in argbRaise where the ground rises and argbLower where it sinks. The composed
	///          height is what a rebuild writes: base + (target - base) x min(1, weight) of PreparedTerrain::Evaluate
	///          (no texture lookup: only heights are drawn). At most `lineLimit` lines are produced: lines are added
	///          in that order until one does not fit (a tick is all or nothing, a polyline is cut at the limit), after
	///          which nothing more is drawn or evaluated: linesTotal keeps counting outline and rect segments exactly
	///          and then adds every remaining grid sample as a tick, so once the budget is spent it is an upper-bound
	///          estimate. Points are not used (the point counts stay 0). Deterministic.
	/// @param prepared The prepared ops.
	/// @param base Base terrain height at a world position.
	/// @param step Grid spacing of the height ticks (m); no ticks if not positive.
	/// @param argbRaise Colour of raising ticks and of the outlines.
	/// @param argbLower Colour of lowering ticks.
	/// @param lineLimit Number of lines produced at most.
	DebugGeometry BuildTerrainDebugGeometry(const PreparedTerrain& prepared, const std::function<float(float, float)>& base,
		float step, uint32 argbRaise, uint32 argbLower, size_t lineLimit);
}
