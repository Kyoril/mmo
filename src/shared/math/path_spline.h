// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	/// @brief How control points are connected.
	enum class SplineInterpolation : uint8
	{
		/// @brief Straight segments between control points.
		Linear,

		/// @brief Uniform Catmull-Rom (tension 0.5) passing through every control point.
		CatmullRom
	};

	/// @brief One control point. `width` is a generic per-point scalar (rivers: channel half width).
	struct SplinePoint
	{
		float x = 0.0f;
		float z = 0.0f;
		float width = 0.0f;
	};

	/// @brief Control point description of a 2D path on the x/z plane.
	struct PathSpline
	{
		std::vector<SplinePoint> points;
		bool closed = false;
		SplineInterpolation interpolation = SplineInterpolation::Linear;
	};

	/// @brief One sample of an evaluated spline; `distance` is the arc length from the first point.
	struct SplineSample
	{
		float x;
		float z;
		float width;
		float distance;
	};

	/// @brief A spline evaluated into a polyline; all queries run on the polyline.
	class SampledPath
	{
	public:
		/// @brief Evaluates the spline.
		/// @param spline Control points.
		/// @param maxStep Maximum chord length of a polyline step; must be positive.
		SampledPath(const PathSpline& spline, float maxStep);

		/// @brief Polyline samples. A closed path does not repeat its first sample at the end.
		const std::vector<SplineSample>& GetSamples() const { return m_samples; }

		/// @brief Total arc length (including the closing segment for closed paths).
		float GetLength() const { return m_length; }

		/// @brief Whether the path is closed.
		bool IsClosed() const { return m_closed; }

		/// @brief Closed: negative inside, positive outside. Open: unsigned distance.
		float SignedDistance(float x, float z) const;

		/// @brief Even-odd containment test. Always false for open paths.
		bool Contains(float x, float z) const;

		/// @brief Samples the path at an arc distance. Wraps for closed paths, clamps for open ones.
		SplineSample SampleAtDistance(float distance) const;

		/// @brief Outward unit normal (closed, either winding) or left normal (open) at an arc distance.
		void NormalAtDistance(float distance, float& nx, float& nz) const;

		/// @brief Bounding box of all samples.
		void GetBounds(float& minX, float& minZ, float& maxX, float& maxZ) const;

		/// @brief Index of the control point segment a sample belongs to (for locality checks).
		uint32 SegmentAtDistance(float distance) const;

		/// @brief Whether any two non-adjacent polyline segments cross.
		bool IsSelfIntersecting() const;

	private:
		/// @brief Finds the polyline segment containing the (already wrapped/clamped) distance.
		size_t FindSegment(float distance) const;

		/// @brief Wraps (closed) or clamps (open) a distance into [0, length].
		float Normalize(float distance) const;

		/// @brief Number of polyline segments.
		size_t GetSegmentCount() const;

		/// @brief End sample of polyline segment i.
		const SplineSample& GetSegmentEnd(size_t i) const;

	private:
		std::vector<SplineSample> m_samples;
		std::vector<uint32> m_controlSegments;
		float m_length = 0.0f;
		bool m_closed = false;
		bool m_windingPositive = true;
	};
}
