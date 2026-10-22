// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	class DeferredRenderer;
	struct EnvironmentState;

	/// Pushes the non-fog part of an environment state (exposure, bloom, light scattering and color
	/// grading) into a deferred renderer. Shared by the world and the glue scene.
	/// @param renderer The renderer to configure.
	/// @param state The blended environment state of this frame.
	/// @param brightness The player's brightness setting, multiplied into the state's exposure.
	void ApplyEnvironmentStateToRenderer(DeferredRenderer& renderer, const EnvironmentState& state, float brightness);
}
