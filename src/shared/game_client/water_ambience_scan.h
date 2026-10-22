// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <vector>

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	/// @brief Water information at one world position (x, z).
	struct WaterSample
	{
		/// @brief Whether there is water at this position. Unloaded terrain reads as dry.
		bool hasWater = false;

		/// @brief Water type id of the water at this position. Only valid when hasWater is set.
		uint32 waterType = 0;

		/// @brief Height of the water surface. Only valid when hasWater is set.
		float surfaceY = 0;

		/// @brief Whether ground height information is available at this position.
		bool hasGround = false;

		/// @brief Height of the ground. Only valid when hasGround is set.
		float groundY = 0;

		/// @brief Flow speed of the water at this position.
		float flowSpeed = 0;
	};

	/// @brief Answers water samples at world positions for the water ambience scan.
	class IWaterAmbienceSource
	{
	public:
		/// @brief Default virtual destructor.
		virtual ~IWaterAmbienceSource() = default;

	public:
		/// @brief Samples water and ground at the given world position.
		/// @param x World x coordinate.
		/// @param z World z coordinate.
		/// @return The water sample at that position.
		virtual WaterSample Sample(float x, float z) const = 0;
	};

	/// @brief Describes which water the scan aggregates into one ambience field.
	struct WaterAmbienceProfile
	{
		/// @brief Water type this profile listens to.
		uint32 waterType = 0;

		/// @brief If set, only water samples next to land (a shore) count.
		bool shoreOnly = false;

		/// @brief Maximum distance in world units at which water still counts.
		float range = 40.0f;
	};

	/// @brief Aggregated result of the water ambience scan for one profile.
	struct WaterAmbienceField
	{
		/// @brief Water type of the profile this field belongs to.
		uint32 waterType = 0;

		/// @brief Loudness level in [0, 1]. 0 means no water of this profile was found.
		float level = 0;

		/// @brief Weighted mean of the unit directions from the ear to the water. Its length is
		///	the focus: close to 1 when the water lies in one direction, close to 0 when it
		///	surrounds the ear.
		Vector3 direction;

		/// @brief Weighted mean flow speed of the water.
		float speed = 0;

		/// @brief Gets how focused the water is in one direction, in [0, 1].
		/// @return The length of direction.
		[[nodiscard]] float GetFocus() const;
	};

	namespace water_ambience
	{
		/// @brief Distance between two scan points: one eighth of a terrain page tile.
		constexpr float GridSpacing = 33.333333f / 8.0f;

		/// @brief Sum of sample weights at which the level saturates at 1.
		constexpr float LevelSaturationWeight = 8.0f;
	}

	/// @brief Scans the water around the ear and aggregates it into one field per profile.
	///
	///	Sample points sit at world-snapped grid quad centres within each profile's range around
	///	the ear, so moving the ear does not make points pop in and out. Every water sample of a
	///	profile's type gets the weight (1 - d / range)^2, where d is the 3D distance from the
	///	ear to the water surface. Buried water (ground above the water surface) contributes
	///	nothing, but still counts as land when it neighbours a shore sample.
	/// @param source Source which answers the water samples.
	/// @param ear World position of the listener.
	/// @param profiles Profiles to aggregate.
	/// @return One field per profile in profile order, including fields with level 0.
	std::vector<WaterAmbienceField> ScanWaterAmbience(const IWaterAmbienceSource& source, const Vector3& ear, const std::vector<WaterAmbienceProfile>& profiles);
}
