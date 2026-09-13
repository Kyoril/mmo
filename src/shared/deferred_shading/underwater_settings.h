// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Everything the underwater post-process pass needs for one frame.
	///
	/// @remark Deliberately free of any graphics dependency so it compiles into the headless
	///			deferred_shading_tests target, which links only base and math. It is also passed to
	///			the renderer as an explicit parameter rather than read from a global, so the
	///			decision logic can be tested without a device.
	struct UnderwaterState
	{
		/// @brief Whether the camera is currently below a water surface.
		bool active{ false };

		/// @brief How far the camera is below the surface, in world units. Never negative.
		float submersionDepth{ 0.0f };

		/// @brief World Y of the water surface above the camera.
		float surfaceHeight{ 0.0f };

		/// @brief Crossing progress in [0,1]: 0 fully dry, 1 fully submerged.
		/// @remark Drives the meniscus band and ramps fog in rather than snapping it, so breaking
		///			the surface does not flash a full-strength filter over one frame.
		float transitionPhase{ 0.0f };

		/// @brief Fog density per world unit. Higher is murkier.
		float fogDensity{ 0.0f };

		/// @brief Linear RGB fog colour.
		float fogColor[3]{ 0.0f, 0.0f, 0.0f };

		/// @brief Beer-Lambert absorption per channel, driving the depth tint.
		float absorptionColor[3]{ 0.0f, 0.0f, 0.0f };

		/// @brief Projected caustics strength. 0 disables them.
		float causticsStrength{ 0.0f };

		/// @brief Screen distortion strength. 0 disables it.
		float distortionStrength{ 0.0f };

		/// @brief Whether sun shafts are drawn. Driven by the gxUnderwaterGodRays cvar.
		bool godRaysEnabled{ true };
	};

	/// @brief Tunables for how the underwater treatment fades in and out.
	struct UnderwaterSettings
	{
		/// @brief Seconds taken to ramp fully between dry and submerged.
		float transitionSeconds{ 0.4f };

		/// @brief Moves the crossing phase toward its target for one frame.
		/// @param current The current phase in [0,1].
		/// @param targetSubmerged True when the camera is below the surface this frame.
		/// @param deltaSeconds Frame time in seconds.
		/// @return The new phase, clamped to [0,1].
		/// @remark A transitionSeconds of 0 snaps rather than dividing by zero.
		[[nodiscard]] float AdvanceTransition(const float current, const bool targetSubmerged,
			const float deltaSeconds) const
		{
			const float target = targetSubmerged ? 1.0f : 0.0f;

			if (transitionSeconds <= 0.0f)
			{
				return target;
			}

			const float step = deltaSeconds / transitionSeconds;

			if (current < target)
			{
				return Clamp(current + step, 0.0f, 1.0f);
			}

			if (current > target)
			{
				return Clamp(current - step, 0.0f, 1.0f);
			}

			return target;
		}

	private:
		/// @brief Local clamp helper. Deliberately avoids pulling in a math header so this struct
		///			stays dependency-free for the headless test build.
		static float Clamp(const float value, const float minValue, const float maxValue)
		{
			if (value < minValue)
			{
				return minValue;
			}

			if (value > maxValue)
			{
				return maxValue;
			}

			return value;
		}
	};

	/// @brief Decides whether the underwater post-process pass has anything to do.
	/// @param state The state for this frame.
	/// @return True when the pass must run.
	/// @remark Deliberately true while the camera is dry but the crossing is still unwinding,
	///			otherwise surfacing would cut the meniscus off mid-animation. When this returns
	///			false the renderer skips the pass entirely and hands back the raw scene texture,
	///			so every editor viewport and the client frame pay exactly nothing for the feature.
	[[nodiscard]] inline bool ShouldRunUnderwaterPass(const UnderwaterState& state)
	{
		return state.active || state.transitionPhase > 0.0f;
	}
}
