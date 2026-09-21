// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Point and spot light math shared by the CPU and the shaders.
	/// @remark LightCommon.hlsli mirrors every function here - change both together. Dependency-free
	///         apart from math so any test suite can compile it.
	namespace light_math
	{
		/// @brief Smallest allowed full spot cone angle, in degrees.
		constexpr float MinConeAngle = 1.0f;

		/// @brief Largest allowed full spot cone angle, in degrees.
		constexpr float MaxConeAngle = 179.0f;

		/// @brief Default full inner cone angle (full strength inside), in degrees.
		constexpr float DefaultInnerConeAngle = 30.0f;

		/// @brief Default full outer cone angle (no light outside), in degrees.
		constexpr float DefaultOuterConeAngle = 45.0f;

		/// @brief Largest per-light fog scattering multiplier.
		constexpr float MaxFogScattering = 8.0f;

		/// @brief Capacity of the group-shared light list of one 8x8x8 froxel block (CS_FogInject.hlsl).
		constexpr uint32 MaxLightsPerFogBlock = 64;

		/// @brief Cosine of half of a full cone angle.
		/// @param fullAngleDegrees Full cone angle in degrees.
		[[nodiscard]] inline float ConeCosine(const float fullAngleDegrees)
		{
			return std::cos(fullAngleDegrees * 0.5f * 3.14159265358979f / 180.0f);
		}

		/// @brief Precomputed spot cone cosines as uploaded in ShaderLight.
		struct SpotCosinePair
		{
			/// @brief Cosine of half the outer cone angle.
			float outer;

			/// @brief Cosine of half the inner cone angle, always above outer.
			float inner;
		};

		/// @brief Converts full inner and outer cone angles (degrees) to shader cosines.
		/// @remark Keeps inner strictly above outer so the shader's smoothstep never has equal edges.
		[[nodiscard]] inline SpotCosinePair SpotCosines(const float innerDegrees, const float outerDegrees)
		{
			SpotCosinePair pair;
			pair.outer = ConeCosine(outerDegrees);
			pair.inner = std::max(ConeCosine(innerDegrees), pair.outer + 1e-4f);
			return pair;
		}

		/// @brief Distance attenuation of point and spot lights: (1 - d / range)^2, 0 at and beyond the range.
		[[nodiscard]] inline float PointAttenuation(const float distance, const float range)
		{
			if (range <= 0.0f || distance >= range)
			{
				return 0.0f;
			}

			const float t = 1.0f - std::clamp(distance / range, 0.0f, 1.0f);
			return t * t;
		}

		/// @brief HLSL smoothstep.
		[[nodiscard]] inline float SmoothStep(const float edge0, const float edge1, const float x)
		{
			const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		/// @brief Spot cone falloff.
		/// @param cosToPoint dot(spot direction, normalized light-to-point direction).
		[[nodiscard]] inline float SpotFactor(const float cosToPoint, const float cosOuter, const float cosInner)
		{
			return SmoothStep(cosOuter, cosInner, cosToPoint);
		}

		/// @brief Whether two spheres overlap or touch.
		[[nodiscard]] inline bool SphereIntersectsSphere(const Vector3& centerA, const float radiusA, const Vector3& centerB, const float radiusB)
		{
			const float reach = radiusA + radiusB;
			return (centerB - centerA).GetSquaredLength() <= reach * reach;
		}

		/// @brief Conservative spot cone (apex, axis, range, half-angle) vs sphere test.
		/// @remark Never returns false for a sphere containing a point of the cone within range; may
		///         return true for spheres just outside it. Half-angles must be below 90 degrees.
		/// @param direction Normalized cone axis.
		/// @param cosHalfAngle Cosine of half the outer cone angle.
		[[nodiscard]] inline bool SpotConeIntersectsSphere(const Vector3& apex, const Vector3& direction, const float range,
			const float cosHalfAngle, const Vector3& center, const float radius)
		{
			const Vector3 toCenter = center - apex;
			const float lengthSquared = toCenter.GetSquaredLength();
			const float along = toCenter.Dot(direction);
			const float sinHalfAngle = std::sqrt(std::max(0.0f, 1.0f - cosHalfAngle * cosHalfAngle));
			const float closest = cosHalfAngle * std::sqrt(std::max(lengthSquared - along * along, 0.0f)) - along * sinHalfAngle;

			const bool outsideAngle = closest > radius;
			const bool beyondRange = along > range + radius;
			const bool behindApex = along < -radius;
			return !(outsideAngle || beyondRange || behindApex);
		}
	}
}
