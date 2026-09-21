// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "fog_volume_math.h"

#include "game_common/fog_volume.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace mmo
{
	/// @brief Builds the per-frame list of fog volume instances to shade with.
	/// @param volumes All authored fog volumes for the current map.
	/// @param hour Current game-time hour in [0, 24), used to evaluate each volume's
	///        time-of-day fade (see fog_volume::TimeOfDayFactor).
	/// @param cameraPosition World-space camera position, used for distance culling and sorting.
	/// @param range Maximum distance, in world units, from the camera's position to a volume's
	///        bounding sphere at which the volume is still considered.
	/// @param isVisible Frustum/occlusion test for a volume's bounding sphere (centre, radius).
	/// @return Instances for volumes whose time-of-day factor is greater than zero, whose
	///         bounding sphere is within `range` of the camera and visible, sorted by ascending
	///         distance to the camera and capped at fog_volume::MaxVolumesPerFrame. Each
	///         instance's density is the volume's density multiplied by its time-of-day factor.
	[[nodiscard]] inline std::vector<FogVolumeInstance> SelectFogVolumes(
		const std::vector<FogVolume>& volumes,
		const float hour,
		const Vector3& cameraPosition,
		const float range,
		const std::function<bool(const Vector3& center, float radius)>& isVisible)
	{
		std::vector<std::pair<float, FogVolumeInstance>> candidates;
		candidates.reserve(volumes.size());

		for (const FogVolume& volume : volumes)
		{
			const float factor = fog_volume::TimeOfDayFactor(hour, volume.activeFrom, volume.activeTo, volume.fadeHours);
			if (factor <= 0.0f)
			{
				continue;
			}

			const float distance = (volume.position - cameraPosition).GetLength();
			const float radius = (volume.size * 0.5f).GetLength();
			if (distance - radius > range)
			{
				continue;
			}

			if (!isVisible(volume.position, radius))
			{
				continue;
			}

			constexpr float degToRad = 3.14159265358979323846f / 180.0f;
			const float yawRad = volume.yaw * degToRad;

			FogVolumeInstance instance;
			instance.center = volume.position;
			instance.density = volume.density * factor;
			instance.halfSize = volume.size * 0.5f;
			instance.heightFalloff = volume.heightFalloff;
			instance.color = volume.color;
			instance.edgeFade = volume.edgeFade;
			instance.yawSin = std::sin(yawRad);
			instance.yawCos = std::cos(yawRad);
			instance.noiseAmount = volume.noiseAmount;
			instance.noiseDetail = static_cast<float>(volume.noiseDetail);
			instance.shape = static_cast<uint32>(volume.shape);

			candidates.emplace_back(distance, instance);
		}

		std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b)
		{
			return a.first < b.first;
		});

		if (candidates.size() > fog_volume::MaxVolumesPerFrame)
		{
			candidates.resize(fog_volume::MaxVolumesPerFrame);
		}

		std::vector<FogVolumeInstance> result;
		result.reserve(candidates.size());
		for (auto& candidate : candidates)
		{
			result.push_back(std::move(candidate.second));
		}
		return result;
	}
}
