// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Per-instance fog volume data prepared for a single frame's shading pass.
	struct FogVolumeInstance
	{
		/// @brief World-space centre of the volume.
		Vector3 center;

		/// @brief Peak fog density at the volume's centre, already scaled by the time-of-day factor.
		float density = 0.0f;

		/// @brief Half-extents of the volume along each local axis.
		Vector3 halfSize;

		/// @brief How strongly density falls off with height inside the volume.
		float heightFalloff = 0.0f;

		/// @brief Tint applied to the fog.
		Vector3 color;

		/// @brief Fraction of the volume's extent over which density fades out towards the edge.
		float edgeFade = 0.0f;

		/// @brief Sine of the volume's yaw, in radians.
		float yawSin = 0.0f;

		/// @brief Cosine of the volume's yaw, in radians.
		float yawCos = 0.0f;

		/// @brief Strength of the animated density noise.
		float noiseAmount = 0.0f;

		/// @brief Noise octave/detail level.
		float noiseDetail = 0.0f;

		/// @brief The shape used to evaluate falloff inside this volume (see FogVolumeShape).
		uint32 shape = 0;
	};
}

/// @remark Mirrored by FogVolumeCommon.hlsli - keep the two in lockstep.
namespace mmo::fog_volume
{
	/// @brief Maximum number of fog volume instances selected for a single frame.
	constexpr uint32 MaxVolumesPerFrame = 64;

	/// @brief Maximum number of fog volume instances a single shading block/tile may reference.
	constexpr uint32 MaxVolumesPerBlock = 16;

	/// @brief Computes how strongly a fog volume should currently contribute based on the
	///        game-time hour, with a linear fade in/out around the active window's edges.
	/// @param hour Current game-time hour in [0, 24).
	/// @param from Hour at which the volume starts being active.
	/// @param to Hour at which the volume stops being active.
	/// @param fadeHours Duration, in hours, of the fade in/out transition. Clamped to at most
	///        half the active window's length.
	/// @return 1 when fully active, 0 when outside the active window, and a value in between
	///         while fading in or out. `from == to` means always on.
	[[nodiscard]] inline float TimeOfDayFactor(const float hour, const float from, const float to, const float fadeHours)
	{
		if (from == to)
		{
			return 1.0f;
		}

		const float length = std::fmod(to - from + 24.0f, 24.0f);
		const float sinceStart = std::fmod(hour - from + 24.0f, 24.0f);
		if (sinceStart >= length)
		{
			return 0.0f;
		}

		const float fade = std::min(fadeHours, length * 0.5f);
		if (fade <= 0.0f)
		{
			return 1.0f;
		}

		const float untilEnd = length - sinceStart;
		return std::clamp(std::min(sinceStart, untilEnd) / fade, 0.0f, 1.0f);
	}

	/// @brief Transforms a world-space position into a fog volume's local space, undoing the
	///        volume's yaw rotation around +Y.
	/// @param worldPos World-space position to transform.
	/// @param center World-space centre of the volume.
	/// @param yawSin Sine of the volume's yaw, in radians.
	/// @param yawCos Cosine of the volume's yaw, in radians.
	/// @return `worldPos` relative to `center`, rotated by -yaw about +Y. Consistent with
	///         `Quaternion(Degree(yaw), Vector3::UnitY) * localPoint + center == worldPoint`.
	[[nodiscard]] inline Vector3 ToLocal(const Vector3& worldPos, const Vector3& center, const float yawSin, const float yawCos)
	{
		const Vector3 d = worldPos - center;
		// Rotation by -yaw about +Y.
		return Vector3(d.x * yawCos - d.z * yawSin, d.y, d.x * yawSin + d.z * yawCos);
	}

	/// @brief Evaluates a normalized shape distance for a point in a fog volume's local space:
	///        0 at the centre, 1 at the shape's surface, and beyond 1 outside it.
	/// @param shape 0 for a box (Chebyshev/L-infinity distance), any other value for an
	///        ellipsoid (Euclidean distance in normalized space).
	/// @param local Point in the volume's local space (see ToLocal).
	/// @param halfSize Half-extents of the volume along each local axis.
	[[nodiscard]] inline float ShapeDistance(const uint32 shape, const Vector3& local, const Vector3& halfSize)
	{
		const Vector3 n(local.x / halfSize.x, local.y / halfSize.y, local.z / halfSize.z);
		if (shape == 1)
		{
			return std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		}
		return std::max(std::abs(n.x), std::max(std::abs(n.y), std::abs(n.z)));
	}

	/// @brief Smoothly fades density to zero over the outer `edgeFade` fraction of the shape.
	/// @param distance Normalized shape distance from ShapeDistance.
	/// @param edgeFade Fraction of the shape's extent, in [0, 1], over which density fades out.
	[[nodiscard]] inline float EdgeFade(const float distance, const float edgeFade)
	{
		if (distance >= 1.0f)
		{
			return 0.0f;
		}
		if (edgeFade <= 0.0f)
		{
			return 1.0f;
		}
		const float t = std::clamp((1.0f - distance) / edgeFade, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	/// @brief Exponential density falloff with height above the volume's floor.
	/// @param local Point in the volume's local space (see ToLocal).
	/// @param halfSize Half-extents of the volume along each local axis.
	/// @param heightFalloff How strongly density falls off with height inside the volume.
	[[nodiscard]] inline float HeightFactor(const Vector3& local, const Vector3& halfSize, const float heightFalloff)
	{
		return std::exp(-heightFalloff * (local.y + halfSize.y));
	}

	/// @brief GPU layout of one fog volume. MUST match struct FogVolume in FogVolumeCommon.hlsli (80 bytes).
	struct alignas(16) GpuFogVolume
	{
		/// @brief World-space centre of the volume.
		float center[3];
		/// @brief Peak density, already scaled by the time-of-day factor.
		float density;
		/// @brief Half-extents along each local axis.
		float halfSize[3];
		/// @brief Exponential density falloff with height above the volume's floor.
		float heightFalloff;
		/// @brief Scattering tint.
		float color[3];
		/// @brief Fraction of the shape over which density fades out towards the edge.
		float edgeFade;
		/// @brief Sine of the volume's yaw.
		float yawSin;
		/// @brief Cosine of the volume's yaw.
		float yawCos;
		/// @brief Strength of the density noise.
		float noiseAmount;
		/// @brief Noise frequency multiplier relative to the zone noise.
		float noiseDetail;
		/// @brief 0 box, 1 ellipsoid.
		uint32 shape;
		/// @brief Pads the record to a 16-byte multiple.
		float padding[3];
	};

	static_assert(sizeof(GpuFogVolume) == 80, "GpuFogVolume must match struct FogVolume in FogVolumeCommon.hlsli");

	/// @brief Converts a selected fog volume instance into its GPU record.
	/// @param instance The instance to convert.
	/// @return The GPU record, padding zeroed.
	[[nodiscard]] inline GpuFogVolume ToGpu(const FogVolumeInstance& instance)
	{
		GpuFogVolume gpu{};
		gpu.center[0] = instance.center.x;
		gpu.center[1] = instance.center.y;
		gpu.center[2] = instance.center.z;
		gpu.density = instance.density;
		gpu.halfSize[0] = instance.halfSize.x;
		gpu.halfSize[1] = instance.halfSize.y;
		gpu.halfSize[2] = instance.halfSize.z;
		gpu.heightFalloff = instance.heightFalloff;
		gpu.color[0] = instance.color.x;
		gpu.color[1] = instance.color.y;
		gpu.color[2] = instance.color.z;
		gpu.edgeFade = instance.edgeFade;
		gpu.yawSin = instance.yawSin;
		gpu.yawCos = instance.yawCos;
		gpu.noiseAmount = instance.noiseAmount;
		gpu.noiseDetail = instance.noiseDetail;
		gpu.shape = instance.shape;
		return gpu;
	}
}
