// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>
#include <utility>

#include "game_client/water_ambience_scan.h"

namespace mmo
{
	/// @brief Water ambience source which answers every sample through a test supplied function.
	class FakeWaterSource final : public IWaterAmbienceSource
	{
	public:
		/// @brief Function which produces the water sample at a world position (x, z).
		using SampleFunction = std::function<WaterSample(float x, float z)>;

	public:
		/// @brief Creates the fake source.
		/// @param sampleFunction Function answering every Sample call.
		explicit FakeWaterSource(SampleFunction sampleFunction)
			: m_sampleFunction(std::move(sampleFunction))
		{
		}

		/// @copydoc IWaterAmbienceSource::Sample
		WaterSample Sample(const float x, const float z) const override
		{
			return m_sampleFunction(x, z);
		}

	private:
		SampleFunction m_sampleFunction;
	};

	/// @brief Builds a water sample of the given type and surface height without ground information.
	inline WaterSample MakeWater(const uint32 waterType, const float surfaceY, const float flowSpeed = 0.0f)
	{
		WaterSample sample;
		sample.hasWater = true;
		sample.waterType = waterType;
		sample.surfaceY = surfaceY;
		sample.flowSpeed = flowSpeed;
		return sample;
	}
}
