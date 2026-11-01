// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph_data.h"

#include "math/path_spline.h"

#include <vector>

namespace mmo::pcg::graph
{
	/// @brief Gets a numeric setting (Float or Int) as float, or the fallback if it is missing, of another
	///        type or not finite.
	float SettingFloat(const AttributeSet& settings, const String& name, float fallback);

	/// @brief Gets a Bool setting, or the fallback if it is missing or of another type.
	bool SettingBool(const AttributeSet& settings, const String& name, bool fallback);

	/// @brief Gets a String setting, or the fallback if it is missing or of another type.
	String SettingString(const AttributeSet& settings, const String& name, const String& fallback);

	/// @brief Splits a comma separated list, trimming spaces and tabs around entries and dropping empty ones.
	std::vector<String> SplitList(const String& list);

	/// @brief An axis-aligned rectangle on the x/z plane.
	struct Bounds2D
	{
		float minX = 0;
		float minZ = 0;
		float maxX = 0;
		float maxZ = 0;

		/// @brief False for a default constructed (empty) rectangle.
		bool valid = false;

		/// @brief Grows the rectangle to contain a point.
		void Add(float x, float z);

		/// @brief Whether two valid rectangles overlap (touching counts).
		bool Intersects(const Bounds2D& other) const;
	};

	/// @brief Conservative x/z bounds of a spline without evaluating it: the control points plus, for curved
	///        splines, their effective tangent handles (a Bezier segment lies inside the hull of its points
	///        and handles). Invalid if the spline has no points.
	Bounds2D SplineBounds(const PathSpline& spline);

	/// @brief Largest coordinate magnitude the nodes process; geometry beyond it (or non-finite) is skipped
	///        with a warning so cell loops and spline evaluation stay bounded.
	constexpr float MaxWorldCoordinate = 1.0e7f;

	/// @brief Whether valid bounds are finite and within MaxWorldCoordinate.
	bool SaneBounds(const Bounds2D& bounds);

	/// @brief Polyline step used to evaluate splines for sampling.
	constexpr float SamplingStep = 0.25f;

	/// @brief Gets the sorted x coordinates where the horizontal line at z crosses a closed polyline (the
	///        closing segment included), with the same segment predicate and intersection formula as
	///        SampledPath::Contains, so InsideCrossings agrees with Contains exactly.
	void PolygonCrossingsAtZ(const std::vector<SplineSample>& polygon, float z, std::vector<float>& crossings);

	/// @brief Whether x lies inside by the even-odd rule, given the sorted crossings of its row.
	bool InsideCrossings(const std::vector<float>& crossings, float x);
}
