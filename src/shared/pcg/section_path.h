// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/path_spline.h"
#include "math/vector3.h"

#include <vector>

namespace mmo::pcg
{
	/// An upright frame on a path. Sections are authored along local +Z with +X to the right.
	struct SectionFrame
	{
		/// World position on the path (height follows the spline).
		Vector3 position;

		/// Horizontal travel direction (unit, x/z).
		float forwardX = 0.0f;
		float forwardZ = 1.0f;

		/// Horizontal right direction (unit, x/z) when looking along forward with +Y up.
		float rightX = 1.0f;
		float rightZ = 0.0f;
	};

	/// One copy of a section, stretched to fit its span.
	struct SectionCopy
	{
		/// Index of the control point span (segment) this copy lies in.
		uint32 span = 0;

		/// Index of the copy inside its span.
		uint32 copy = 0;

		/// Arc distance of the copy's start from the path start.
		float startDistance = 0.0f;

		/// Arc length the copy covers (the section stretched to fit).
		float stretchedLength = 0.0f;

		/// Stable key of the span, derived from its two control point ids.
		uint64 spanKey = 0;
	};

	/// Result of fitting sections along a spline.
	struct SectionLayout
	{
		/// Copies ordered along the spline.
		std::vector<SectionCopy> copies;

		/// Smallest x/z radius of curvature found; the largest float for a path without any bend.
		float minimumBendRadius = 0.0f;
	};

	/// Sample step every path handed to this API must be built with, so arc distances agree between the
	/// layout, frames, projection and copy lookup.
	constexpr float kSectionPathSampleStep = 0.5f;

	/// Fits whole copies of a section into every control point span; each span gets
	/// max(1, round(spanLength / sectionLength)) copies stretched evenly to the span.
	/// With Catmull-Rom auto tangents moving a point also reshapes the neighbouring spans (span keys stay stable).
	/// @param spline The control points the path was sampled from.
	/// @param path The spline sampled with kSectionPathSampleStep.
	/// @param pointIds Stable id per control point (same size as spline.points); spanKey = Hash(id_j, id_{j+1}).
	/// @param sectionLength Authored length of the section along +Z; must be positive.
	SectionLayout LayOutSections(const PathSpline& spline, const SampledPath& path, const std::vector<uint64>& pointIds, float sectionLength);

	/// Upright frame at an arc distance (clamped or wrapped like SampledPath).
	SectionFrame FrameAt(const SampledPath& path, float distance);

	/// Projects a world point onto the nearest point of the path.
	/// @param distance Receives the arc distance of that point.
	/// @param across Receives the signed offset to the right of the path (negative = left).
	/// @return False if the point is further than maxAcross from the path.
	bool ProjectToPath(const SampledPath& path, float x, float z, float maxAcross, float& distance, float& across);

	/// Finds the copy containing an arc distance.
	/// @param localZ Receives the position inside the copy mapped to 0..sectionLength.
	/// @return The copy, or nullptr if the distance is outside every copy.
	const SectionCopy* FindCopy(const SectionLayout& layout, float distance, float sectionLength, float& localZ);
}
