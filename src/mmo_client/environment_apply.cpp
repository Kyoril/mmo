// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_apply.h"

#include "deferred_shading/color_grading.h"
#include "deferred_shading/deferred_renderer.h"
#include "scene_graph/environment_state.h"

namespace mmo
{
	void ApplyEnvironmentStateToRenderer(DeferredRenderer& renderer, const EnvironmentState& state, const float brightness)
	{
		renderer.SetExposure(state.exposure * brightness);
		renderer.SetBloomIntensity(state.bloomIntensity);
		renderer.SetBloomThreshold(state.bloomThreshold);
		renderer.SetFogLightScattering(state.lightScattering);

		ColorGradingSettings grading;
		grading.SetSaturation(state.saturation);
		grading.SetContrast(state.contrast);
		grading.SetColorFilter(state.colorFilter);
		renderer.SetColorGrading(grading, state.colorLut, state.colorLutFrom, state.colorLutBlend);
	}
}
