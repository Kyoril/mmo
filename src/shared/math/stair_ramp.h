// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// @brief Collision geometry with a straight staircase's steps replaced by a ramp.
	struct StairRampResult
	{
		/// @brief Whether a ramp was built. If not, `message` says why and the geometry is empty.
		bool success { false };

		/// @brief What was done, or why nothing was.
		String message;

		std::vector<Vector3> vertices;
		std::vector<uint32> indices;

		/// @brief One submesh index per face; empty if the input had none.
		std::vector<uint16> faceSubMeshes;

		/// @brief Faces of the steps that the ramp replaced.
		uint32 removedFaces { 0 };

		/// @brief Incline of the ramp in degrees.
		float slopeDegrees { 0.0f };
	};

	/// @brief Replaces the steps of a straight staircase's collision geometry with a ramp.
	///
	/// Carved steps make poor walking surfaces: uneven treads and short goings defeat both the
	/// player's step-up and the navigation mesh's climb limit. A ramp from the foot of the stairs
	/// (at the geometry's lowest point) to the front of the topmost tread walks smoothly and
	/// rasterizes into one continuous slope.
	///
	/// The run direction and incline come from a plane fitted through the treads (flat, upward
	/// facing triangles above the base). Treads, risers and nosings - triangles within the treads'
	/// footprint that reach to within half a unit of the ramp - are dropped; side faces (stringers,
	/// walls) and everything well below the ramp (the underside) are kept, as is the top landing.
	/// The ramp is wound like the treads it replaces and takes over their most common submesh.
	///
	/// @param vertices Collision vertices.
	/// @param indices Triangle list (3 per face).
	/// @param faceSubMeshes Submesh per face, or empty.
	/// @return The new geometry, or failure if the geometry does not look like a straight staircase
	///         (no treads, or an incline outside 10 to 55 degrees).
	StairRampResult BuildStairRamp(const std::vector<Vector3>& vertices, const std::vector<uint32>& indices, const std::vector<uint16>& faceSubMeshes);
}
