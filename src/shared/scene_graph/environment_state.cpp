// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_state.h"

namespace mmo
{
	namespace
	{
		float lerpFloat(const float a, const float b, const float t)
		{
			return a + (b - a) * t;
		}

		Vector3 toVector3(const Vector4& value)
		{
			return Vector3(value.x, value.y, value.z);
		}
	}

	EnvironmentState EvaluateEnvironment(const EnvironmentProfile& profile, const float normalizedTime)
	{
		EnvironmentState state;

		state.skyHorizon = profile.skyHorizon.Evaluate(normalizedTime);
		state.skyZenith = profile.skyZenith.Evaluate(normalizedTime);
		state.clouds = profile.clouds.Evaluate(normalizedTime);
		state.ambient = toVector3(profile.ambient.Evaluate(normalizedTime));

		const Vector4 sun = profile.sun.Evaluate(normalizedTime);
		state.sunColor = toVector3(sun);
		state.sunIntensity = sun.w;

		const Vector4 moon = profile.moon.Evaluate(normalizedTime);
		state.moonColor = toVector3(moon);
		state.moonIntensity = moon.w;

		const Vector4 fog = profile.fog.Evaluate(normalizedTime);
		state.timeOfDay.fogTint[0] = fog.x;
		state.timeOfDay.fogTint[1] = fog.y;
		state.timeOfDay.fogTint[2] = fog.z;
		state.timeOfDay.densityMultiplier = fog.w;

		const Vector4 scatter = profile.sunScatter.Evaluate(normalizedTime);
		state.timeOfDay.sunScatterColor[0] = scatter.x;
		state.timeOfDay.sunScatterColor[1] = scatter.y;
		state.timeOfDay.sunScatterColor[2] = scatter.z;
		state.timeOfDay.shaftMultiplier = scatter.w;

		state.atmosphere = profile.atmosphere;
		state.exposure = profile.exposure;
		state.bloomIntensity = profile.bloomIntensity;
		state.bloomThreshold = profile.bloomThreshold;

		return state;
	}

	EnvironmentState LerpEnvironment(const EnvironmentState& a, const EnvironmentState& b, const float t)
	{
		EnvironmentState state;

		state.skyHorizon = a.skyHorizon + (b.skyHorizon - a.skyHorizon) * t;
		state.skyZenith = a.skyZenith + (b.skyZenith - a.skyZenith) * t;
		state.clouds = a.clouds + (b.clouds - a.clouds) * t;
		state.ambient = a.ambient + (b.ambient - a.ambient) * t;
		state.sunColor = a.sunColor + (b.sunColor - a.sunColor) * t;
		state.sunIntensity = lerpFloat(a.sunIntensity, b.sunIntensity, t);
		state.moonColor = a.moonColor + (b.moonColor - a.moonColor) * t;
		state.moonIntensity = lerpFloat(a.moonIntensity, b.moonIntensity, t);

		// Both inputs are already inside the setter ranges, so a lerp stays inside them.
		state.atmosphere.density = lerpFloat(a.atmosphere.density, b.atmosphere.density, t);
		state.atmosphere.heightFalloff = lerpFloat(a.atmosphere.heightFalloff, b.atmosphere.heightFalloff, t);
		state.atmosphere.baseHeight = lerpFloat(a.atmosphere.baseHeight, b.atmosphere.baseHeight, t);
		state.atmosphere.anisotropy = lerpFloat(a.atmosphere.anisotropy, b.atmosphere.anisotropy, t);
		state.atmosphere.shaftStrength = lerpFloat(a.atmosphere.shaftStrength, b.atmosphere.shaftStrength, t);

		for (int i = 0; i < 3; ++i)
		{
			state.timeOfDay.fogTint[i] = lerpFloat(a.timeOfDay.fogTint[i], b.timeOfDay.fogTint[i], t);
			state.timeOfDay.sunScatterColor[i] = lerpFloat(a.timeOfDay.sunScatterColor[i], b.timeOfDay.sunScatterColor[i], t);
		}
		state.timeOfDay.densityMultiplier = lerpFloat(a.timeOfDay.densityMultiplier, b.timeOfDay.densityMultiplier, t);
		state.timeOfDay.shaftMultiplier = lerpFloat(a.timeOfDay.shaftMultiplier, b.timeOfDay.shaftMultiplier, t);

		state.exposure = lerpFloat(a.exposure, b.exposure, t);
		state.bloomIntensity = lerpFloat(a.bloomIntensity, b.bloomIntensity, t);
		state.bloomThreshold = lerpFloat(a.bloomThreshold, b.bloomThreshold, t);

		return state;
	}
}
