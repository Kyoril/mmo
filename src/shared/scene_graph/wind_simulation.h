// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "scene_graph/environment_state.h"
#include "scene_graph/wind_state.h"

namespace mmo
{
	/// @brief Smooth 1D value noise over time, in [-1, 1]. Varies on a time scale of a few seconds.
	/// @param time Accumulated seconds. Never pass an absolute clock value.
	[[nodiscard]] float WindGustNoise(double time);

	/// @brief Turns a zone's wind settings into gusting wind and a scrolling fog noise offset.
	/// @remark Pure logic: no graphics. Main thread only. Time and offsets accumulate in doubles and only
	///         wrapped values reach float math, so long sessions keep full precision.
	class WindSimulation final
	{
	public:
		/// @brief Largest relative speed change from gusts at gustiness 1.
		static constexpr float MaxGustSpeedFactor = 0.6f;

		/// @brief Largest direction wobble from gusts at gustiness 1, in degrees.
		static constexpr float MaxGustAngleDegrees = 25.0f;

	public:
		/// @brief Advances the wind by one frame.
		/// @param deltaSeconds Real time since the last update.
		/// @param environment The blended environment state of this frame.
		void Update(float deltaSeconds, const EnvironmentState& environment);

		/// @brief The wind of the last update.
		[[nodiscard]] const WindState& GetState() const { return m_state; }

		/// @brief Clears accumulated time and offsets.
		void Reset();

	private:
		double m_time = 0.0;
		double m_offsetX = 0.0;
		double m_offsetZ = 0.0;
		WindState m_state;
	};

	/// @brief Publishes the WindDirection global shader parameter (xyz direction, w speed).
	void PublishWindShaderParameters(const WindState& state);
}
