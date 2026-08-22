// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/constants.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace terrain
	{
		/// @brief Which side of the reference plane the Flatten brush may move terrain from.
		namespace flatten_mode
		{
			enum Type
			{
				/// Move terrain toward the plane from either side.
				Both,

				/// Only lift terrain that sits below the plane. Anything already at or above it is
				/// left exactly as it is, so detail above the plane survives the stroke.
				RaiseOnly,

				/// Only cut terrain that sits above the plane, leaving anything below it untouched.
				LowerOnly,

				Count_
			};
		}

		/// @brief Steepest slope the plane is allowed to take.
		///
		/// The height gradient is tan(slope), which runs away to infinity at 90 degrees. A plane
		/// that steep is a wall the brush could never resolve terrain against anyway, so the angle
		/// is clamped short of it.
		constexpr float MaxFlattenSlopeDegrees = 89.0f;

		/// @brief The reference surface the Flatten brush drives terrain toward.
		///
		/// A flatten brush with a single target height can only ever produce level ground, which
		/// leaves a ramp a matter of nudging a sculpt brush until it looks straight. Carrying a
		/// plane instead -- an anchor the surface passes through, plus how steeply and in which
		/// direction it falls away from there -- makes a ramp the same one-stroke operation a flat
		/// pad already is. A slope of zero degenerates to the classic horizontal flatten, so the
		/// plane subsumes the target height it replaces rather than sitting beside it.
		///
		/// The anchor is a world position rather than just a height because a tilted plane is only
		/// pinned once you say where. Everything else follows from it, which is what lets the brush
		/// wander far from the anchor and still land on the same continuous ramp.
		struct FlattenPlane
		{
			/// World position the plane passes through.
			Vector3 anchor{ Vector3::Zero };

			/// Angle between the plane and horizontal, in degrees. Zero is level.
			float slopeDegrees{ 0.0f };

			/// Compass direction the plane descends toward, in degrees around +Y. Zero descends
			/// toward +Z; increasing values rotate toward +X.
			float azimuthDegrees{ 0.0f };

			/// @brief The height of the plane above a world XZ position.
			/// @param x World X position.
			/// @param z World Z position.
			/// @return The plane's height there.
			[[nodiscard]] float HeightAt(const float x, const float z) const
			{
				const float slope = std::min(std::abs(slopeDegrees), MaxFlattenSlopeDegrees) * Deg2Rad;
				if (slope <= 0.0f)
				{
					return anchor.y;
				}

				const float azimuth = azimuthDegrees * Deg2Rad;
				const float descentX = std::sin(azimuth);
				const float descentZ = std::cos(azimuth);

				// Distance travelled along the descent direction, which is the only direction the
				// height changes in -- moving across the slope stays level by construction.
				const float along = (x - anchor.x) * descentX + (z - anchor.z) * descentZ;

				return anchor.y - std::tan(slope) * along;
			}

			/// @brief Wraps an angle in degrees into [0, 360).
			/// @param degrees The angle to wrap.
			/// @return The equivalent angle in [0, 360).
			[[nodiscard]] static float NormalizeAzimuth(float degrees)
			{
				degrees = std::fmod(degrees, 360.0f);
				return degrees < 0.0f ? degrees + 360.0f : degrees;
			}

			/// @brief Builds the plane through two world points, descending from the first toward
			///        the second.
			///
			/// Two points leave the roll about the line between them undefined, and this resolves
			/// it by keeping the plane level across that line: the ramp runs from one point to the
			/// other with no sideways cant, which is what "make a ramp from here to there" means.
			///
			/// @param anchor The world point the plane is pinned at.
			/// @param through A second world point the plane also passes through.
			/// @return The plane through both points, or a level plane at the anchor if the two
			///         points sit on top of each other in the XZ plane.
			[[nodiscard]] static FlattenPlane FromTwoPoints(const Vector3& anchor, const Vector3& through)
			{
				const float deltaX = through.x - anchor.x;
				const float deltaZ = through.z - anchor.z;
				const float horizontal = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);

				// Vertically stacked points say nothing about a direction to fall in.
				if (horizontal <= 0.0f)
				{
					return FlattenPlane{ anchor, 0.0f, 0.0f };
				}

				// Positive when the second point is the lower of the two, which is when the plane
				// descends toward it. When it is the higher one the plane descends the other way,
				// so the direction is flipped rather than letting the angle go negative.
				const float drop = (anchor.y - through.y) / horizontal;

				const float descentX = drop >= 0.0f ? deltaX : -deltaX;
				const float descentZ = drop >= 0.0f ? deltaZ : -deltaZ;

				FlattenPlane plane;
				plane.anchor = anchor;
				plane.slopeDegrees = std::min(std::atan(std::abs(drop)) * Rad2Deg, MaxFlattenSlopeDegrees);
				plane.azimuthDegrees = NormalizeAzimuth(std::atan2(descentX, descentZ) * Rad2Deg);
				return plane;
			}

			/// @brief Builds the plane with a given surface normal, pinned at a world point.
			///
			/// This is how a slope is lifted off ground that already exists: sample the terrain
			/// normal under the cursor and the plane arrives parallel to the hillside there.
			///
			/// @param anchor The world point the plane is pinned at.
			/// @param normal The surface normal, which need not be normalized. A normal with no
			///               upward component describes a wall and yields a level plane instead.
			/// @return The plane through the anchor perpendicular to the normal.
			[[nodiscard]] static FlattenPlane FromNormal(const Vector3& anchor, const Vector3& normal)
			{
				// A downward-facing normal describes the same plane as its opposite, so it is
				// folded up rather than rejected.
				const float upward = std::abs(normal.y);
				const float horizontal = std::sqrt(normal.x * normal.x + normal.z * normal.z);

				if (upward <= 0.0f || horizontal <= 0.0f)
				{
					return FlattenPlane{ anchor, 0.0f, 0.0f };
				}

				const float flip = normal.y < 0.0f ? -1.0f : 1.0f;

				FlattenPlane plane;
				plane.anchor = anchor;
				plane.slopeDegrees = std::min(std::atan(horizontal / upward) * Rad2Deg, MaxFlattenSlopeDegrees);
				plane.azimuthDegrees = NormalizeAzimuth(std::atan2(normal.x * flip, normal.z * flip) * Rad2Deg);
				return plane;
			}

			/// @brief The upward-facing unit normal of this plane.
			/// @return The plane's normal, pointing up.
			[[nodiscard]] Vector3 GetNormal() const
			{
				const float slope = std::min(std::abs(slopeDegrees), MaxFlattenSlopeDegrees) * Deg2Rad;
				const float azimuth = azimuthDegrees * Deg2Rad;

				// The normal tilts away from +Y by the slope angle, opposite the descent direction.
				const float sinSlope = std::sin(slope);
				return Vector3(sinSlope * std::sin(azimuth), std::cos(slope), sinSlope * std::cos(azimuth));
			}
		};

		/// @brief Whether a vertex may be moved toward the plane under the given mode.
		/// @param currentHeight The vertex's current height.
		/// @param targetHeight The plane's height above that vertex.
		/// @param mode Which direction of movement the brush allows.
		/// @return True if the brush should touch this vertex at all.
		[[nodiscard]] inline bool FlattenAffectsVertex(const float currentHeight, const float targetHeight, const flatten_mode::Type mode)
		{
			switch (mode)
			{
			case flatten_mode::RaiseOnly:
				return currentHeight < targetHeight;
			case flatten_mode::LowerOnly:
				return currentHeight > targetHeight;
			default:
				return true;
			}
		}

		/// @brief The height a vertex ends up at after one application of the Flatten brush.
		///
		/// Soft flattening eases toward the plane, so dwelling longer converges on it and the
		/// falloff band never quite arrives -- which is what blends a ramp into the ground around
		/// it. Hard flattening drops the easing wherever the brush is at full strength, so the core
		/// of the footprint lands exactly on the plane in a single pass instead of creeping toward
		/// it. That is the difference between a ramp that sits in the landscape and a road or a
		/// building pad that has to be genuinely flat.
		///
		/// Only the core is settled in one pass. The falloff band still blends by weight, and
		/// because each application blends again from wherever the last one left it, a brush held
		/// still goes on tightening the edge. Dwelling is therefore how hard the transition ends
		/// up being: a passing stroke leaves a soft shoulder, a long one leaves a sharp rim.
		///
		/// @param currentHeight The vertex's current height.
		/// @param targetHeight The plane's height above that vertex.
		/// @param factor The brush's falloff weight at that vertex, 1 at full strength.
		/// @param power Dwell-driven strength, already scaled by the frame's time slice.
		/// @param mode Which direction of movement the brush allows.
		/// @param hard True to snap to the plane at full strength rather than easing toward it.
		/// @return The vertex's new height, or its current height if the mode excludes it.
		[[nodiscard]] inline float FlattenVertexHeight(const float currentHeight, const float targetHeight,
			const float factor, const float power, const flatten_mode::Type mode, const bool hard)
		{
			if (!FlattenAffectsVertex(currentHeight, targetHeight, mode))
			{
				return currentHeight;
			}

			if (hard)
			{
				// Inside the inner radius the falloff weight saturates at 1 and the vertex is set
				// outright. The band outside it blends by weight rather than by dwell, so a single
				// pass leaves a proper shoulder -- though repeated passes over the same band still
				// close on the plane, which is what lets a dwell sharpen the rim deliberately.
				const float weight = std::min(std::max(factor, 0.0f), 1.0f);
				return currentHeight + (targetHeight - currentHeight) * weight;
			}

			// Easing. The step is clamped so that a large power, or a long frame, settles on the
			// plane instead of overshooting through it and inverting the terrain it was levelling.
			const float step = std::min(std::max(factor * power, 0.0f), 1.0f);
			return currentHeight + (targetHeight - currentHeight) * step;
		}
	}
}
