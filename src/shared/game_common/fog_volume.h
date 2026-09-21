// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief The volumetric shape a fog volume is rendered as.
	enum class FogVolumeShape : uint8
	{
		/// @brief An axis-aligned (yaw-rotated) box, extending +/- size/2 on each axis from its centre.
		Box = 0,

		/// @brief An ellipsoid inscribed into the box described by position/size/yaw.
		Ellipsoid = 1
	};

	/// @brief A single authored, client-only local fog volume placed in the world editor.
	/// @details Purely a visual effect - it has no gameplay or collision meaning and is never
	///          sent to or evaluated by the server. Instances are persisted per map in a
	///          `Worlds/<dir>/<dir>.hfog` file alongside the map's other world-authoring data.
	struct FogVolume
	{
		/// @brief Stable unique identifier for this volume, used for editing and selection.
		uint32 id = 0;

		/// @brief Optional human-readable name shown in editor tooling.
		String name;

		/// @brief The shape used to evaluate falloff inside this volume.
		FogVolumeShape shape = FogVolumeShape::Box;

		/// @brief World-space centre of the volume.
		Vector3 position;

		/// @brief Full extents (box edge lengths / ellipsoid diameters) of the volume along each local
		///        axis; the renderer halves this to get the box/ellipsoid half-size. Clamped to a
		///        minimum of 0.5 per axis.
		Vector3 size{ 20.0f, 6.0f, 20.0f };

		/// @brief Rotation around the world-space Y axis, in degrees.
		float yaw = 0.0f;

		/// @brief Peak fog density at the volume's centre. Clamped to [0, 1].
		float density = 0.02f;

		/// @brief Tint applied to the fog. Each channel is clamped to [0, 2] to allow moderate over-saturation.
		Vector3 color{ 1.0f, 1.0f, 1.0f };

		/// @brief Fraction of the volume's extent over which density fades out towards the edge. Clamped to [0, 1].
		float edgeFade = 0.3f;

		/// @brief How strongly density falls off with height inside the volume. Clamped to [0, 1].
		float heightFalloff = 0.1f;

		/// @brief Game-time hour (0-24, exclusive upper bound) at which the volume starts fading in.
		float activeFrom = 0.0f;

		/// @brief Game-time hour (0-24, exclusive upper bound) at which the volume starts fading out.
		float activeTo = 0.0f;

		/// @brief Duration, in hours, of the fade in/out transition. Clamped to [0, 6].
		float fadeHours = 1.0f;

		/// @brief Strength of the animated density noise. Clamped to [0, 1].
		float noiseAmount = 0.4f;

		/// @brief Noise octave/detail level. Must be one of 1, 2 or 4; any other value resets to 1.
		uint8 noiseDetail = 1;
	};

	/// @brief Clamps every field of a fog volume into its valid range, as documented on
	///        each field of FogVolume above. Any non-finite float is first replaced by the
	///        corresponding field's default value before being clamped.
	/// @param volume The fog volume to sanitize in place.
	inline void SanitizeFogVolume(FogVolume& volume)
	{
		const FogVolume defaults;

		const auto sanitizeFloat = [](float& value, const float defaultValue)
		{
			if (!std::isfinite(value))
			{
				value = defaultValue;
			}
		};

		sanitizeFloat(volume.size.x, defaults.size.x);
		sanitizeFloat(volume.size.y, defaults.size.y);
		sanitizeFloat(volume.size.z, defaults.size.z);
		sanitizeFloat(volume.yaw, defaults.yaw);
		sanitizeFloat(volume.density, defaults.density);
		sanitizeFloat(volume.color.x, defaults.color.x);
		sanitizeFloat(volume.color.y, defaults.color.y);
		sanitizeFloat(volume.color.z, defaults.color.z);
		sanitizeFloat(volume.edgeFade, defaults.edgeFade);
		sanitizeFloat(volume.heightFalloff, defaults.heightFalloff);
		sanitizeFloat(volume.activeFrom, defaults.activeFrom);
		sanitizeFloat(volume.activeTo, defaults.activeTo);
		sanitizeFloat(volume.fadeHours, defaults.fadeHours);
		sanitizeFloat(volume.noiseAmount, defaults.noiseAmount);
		sanitizeFloat(volume.position.x, defaults.position.x);
		sanitizeFloat(volume.position.y, defaults.position.y);
		sanitizeFloat(volume.position.z, defaults.position.z);

		const auto wrapHours = [](const float hours)
		{
			float wrapped = std::fmod(hours, 24.0f);
			if (wrapped < 0.0f)
			{
				wrapped += 24.0f;
			}
			return wrapped >= 24.0f ? 0.0f : wrapped;
		};

		if (volume.shape != FogVolumeShape::Box && volume.shape != FogVolumeShape::Ellipsoid)
		{
			volume.shape = FogVolumeShape::Box;
		}

		volume.size = Vector3(std::max(volume.size.x, 0.5f), std::max(volume.size.y, 0.5f), std::max(volume.size.z, 0.5f));
		volume.density = std::clamp(volume.density, 0.0f, 1.0f);
		volume.color = Vector3(std::clamp(volume.color.x, 0.0f, 2.0f), std::clamp(volume.color.y, 0.0f, 2.0f), std::clamp(volume.color.z, 0.0f, 2.0f));
		volume.edgeFade = std::clamp(volume.edgeFade, 0.0f, 1.0f);
		volume.heightFalloff = std::clamp(volume.heightFalloff, 0.0f, 1.0f);
		volume.activeFrom = wrapHours(volume.activeFrom);
		volume.activeTo = wrapHours(volume.activeTo);
		volume.fadeHours = std::clamp(volume.fadeHours, 0.0f, 6.0f);
		volume.noiseAmount = std::clamp(volume.noiseAmount, 0.0f, 1.0f);
		if (volume.noiseDetail != 1 && volume.noiseDetail != 2 && volume.noiseDetail != 4)
		{
			volume.noiseDetail = 1;
		}
	}
}
