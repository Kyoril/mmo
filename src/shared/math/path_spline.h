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

		/// @brief Curve through every control point, evaluated as one cubic Bezier per segment from the
		///        points' tangents. Points with automatic tangents get the uniform Catmull-Rom ones
		///        (tension 0.5), so an all-auto spline has the classic Catmull-Rom shape.
		CatmullRom
	};

	/// @brief One control point. `width` is a generic per-point scalar (rivers: channel half width).
	///        The tangent handles only shape curved (CatmullRom) splines; Linear ones ignore them.
	///        Curved splines are 3D curves (the height `y` is interpolated as well); every planar
	///        query of SampledPath (distances, containment, normals, arc length) still works on x/z.
	struct SplinePoint
	{
		float x = 0.0f;
		float z = 0.0f;
		float width = 0.0f;

		/// @brief Height of the point. Interpolated by curved splines; Linear splines carry it along
		///        but none of their users read it.
		float y = 0.0f;

		/// @brief Whether the tangents are computed from the neighbours (Catmull-Rom) instead of
		///        taken from the stored handles below.
		bool autoTangents = true;

		/// @brief Arriving handle x, relative to the point (normally points backwards along the curve).
		float inX = 0.0f;

		/// @brief Arriving handle z, relative to the point.
		float inZ = 0.0f;

		/// @brief Leaving handle x, relative to the point (normally points forwards along the curve).
		float outX = 0.0f;

		/// @brief Leaving handle z, relative to the point.
		float outZ = 0.0f;

		/// @brief Arriving handle height, relative to the point.
		float inY = 0.0f;

		/// @brief Leaving handle height, relative to the point.
		float outY = 0.0f;
	};

	/// @brief Both tangent handles of a point in 3D, relative to the point.
	struct SplineTangents
	{
		float inX = 0.0f;
		float inY = 0.0f;
		float inZ = 0.0f;
		float outX = 0.0f;
		float outY = 0.0f;
		float outZ = 0.0f;
	};

	/// @brief Control point description of a path. Planar queries use x/z; curved splines also
	///        interpolate the height.
	struct PathSpline
	{
		std::vector<SplinePoint> points;
		bool closed = false;
		SplineInterpolation interpolation = SplineInterpolation::Linear;
	};

	/// @brief Gets the tangent handles a point is evaluated with: the stored ones for a manual point,
	///        the uniform Catmull-Rom ones for an automatic point (out = (next - previous) / 6,
	///        in = -out; closed splines wrap, open ones repeat their end points).
	/// @param spline The spline.
	/// @param index Index of the point; an out of range index yields zero handles.
	/// @param inX Receives the arriving handle x, relative to the point.
	/// @param inZ Receives the arriving handle z, relative to the point.
	/// @param outX Receives the leaving handle x, relative to the point.
	/// @param outZ Receives the leaving handle z, relative to the point.
	void GetEffectiveTangents(const PathSpline& spline, size_t index, float& inX, float& inZ, float& outX, float& outZ);

	/// @brief Gets the 3D tangent handles a point is evaluated with (same rules as the planar overload,
	///        applied to the height too).
	/// @param spline The spline.
	/// @param index Index of the point; an out of range index yields zero handles.
	SplineTangents GetEffectiveTangents(const PathSpline& spline, size_t index);

	/// @brief Stores a point's effective tangents and marks it manual, so switching to manual never
	///        moves the curve. No-op for an out of range index.
	void MakeTangentsManual(PathSpline& spline, size_t index);

	/// @brief One sample of an evaluated spline; `distance` is the arc length on the x/z plane from the
	///        first point.
	struct SplineSample
	{
		float x;
		float z;
		float width;
		float distance;

		/// @brief Height of the curve at the sample.
		float y = 0.0f;
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

		/// @brief Total arc length on the x/z plane (including the closing segment for closed paths).
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

		/// @brief Arc distance (x/z) of a control point from the first point. The end point of an open path
		///        sits at the total length; a closed path's wrap-around is not a control point of its own.
		/// @param index Index of the control point; an out of range index yields the total length.
		float GetControlPointDistance(size_t index) const;

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
		std::vector<size_t> m_controlSampleIndices;
		std::vector<float> m_controlDistances;
		float m_length = 0.0f;
		bool m_closed = false;
		bool m_windingPositive = true;
	};
}
