// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_state.h"

#include <cmath>

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

	Vector3 WindDirectionFromDegrees(const float degrees)
	{
		constexpr float degreesToRadians = 3.14159265358979f / 180.0f;
		const float radians = degrees * degreesToRadians;
		return Vector3(std::sin(radians), 0.0f, std::cos(radians));
	}

	float DefaultEnvironmentBrightness(const float normalizedTime)
	{
		float time = std::fmod(normalizedTime, 1.0f);
		if (time < 0.0f)
		{
			time += 1.0f;
		}

		// Full night before dawn and after dusk, full day in between, smoothstepped across the two
		// twilight bands so the lift never shows up as a step. The band edges bracket the dawn and
		// dusk keys of the Default fog curve (0.27 and 0.73).
		constexpr float dawnStart = 0.2f;
		constexpr float dawnEnd = 0.3f;
		constexpr float duskStart = 0.7f;
		constexpr float duskEnd = 0.8f;

		float dayAmount;
		if (time <= dawnStart || time >= duskEnd)
		{
			dayAmount = 0.0f;
		}
		else if (time >= dawnEnd && time <= duskStart)
		{
			dayAmount = 1.0f;
		}
		else
		{
			const float edge = time < dawnEnd ? (time - dawnStart) / (dawnEnd - dawnStart) : (duskEnd - time) / (duskEnd - duskStart);
			dayAmount = edge * edge * (3.0f - 2.0f * edge);
		}

		return lerpFloat(DefaultNightBrightness, DefaultDayBrightness, dayAmount);
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

		const Vector4 distanceFog = profile.distanceFog.Evaluate(normalizedTime);
		state.timeOfDay.distanceFogColor[0] = distanceFog.x;
		state.timeOfDay.distanceFogColor[1] = distanceFog.y;
		state.timeOfDay.distanceFogColor[2] = distanceFog.z;
		state.timeOfDay.distanceFogOpacity = distanceFog.w;

		state.atmosphere = profile.atmosphere;
		state.exposure = profile.exposure;
		state.bloomIntensity = profile.bloomIntensity;
		state.bloomThreshold = profile.bloomThreshold;

		state.windDirection = WindDirectionFromDegrees(profile.windDirectionDegrees);
		state.windSpeed = profile.windSpeed;
		state.windGustiness = profile.windGustiness;
		state.fogNoiseAmount = profile.fogNoiseAmount;
		state.fogNoiseSize = profile.fogNoiseSize;
		state.lightScattering = profile.lightScattering;

		state.colorLut = profile.colorLut;
		state.colorLutFrom.clear();
		state.colorLutBlend = 1.0f;
		state.saturation = profile.saturation;
		state.contrast = profile.contrast;
		state.colorFilter = profile.colorFilter;

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

		// A profile without distance fog blends as one whose fog starts and ends at the far end of the
		// other's ramp, so crossing into or out of a distance-fogged zone pushes the fog wall outward
		// instead of popping it.
		{
			constexpr float farAway = 100000.0f;
			const bool aOn = a.atmosphere.HasDistanceFog();
			const bool bOn = b.atmosphere.HasDistanceFog();
			const float aStart = aOn ? a.atmosphere.distanceFogStart : (bOn ? farAway : 0.0f);
			const float aEnd = aOn ? a.atmosphere.distanceFogEnd : (bOn ? farAway * 2.0f : 0.0f);
			const float bStart = bOn ? b.atmosphere.distanceFogStart : (aOn ? farAway : 0.0f);
			const float bEnd = bOn ? b.atmosphere.distanceFogEnd : (aOn ? farAway * 2.0f : 0.0f);
			state.atmosphere.distanceFogStart = lerpFloat(aStart, bStart, t);
			state.atmosphere.distanceFogEnd = lerpFloat(aEnd, bEnd, t);
			state.atmosphere.distanceFogSkyBand = lerpFloat(a.atmosphere.distanceFogSkyBand, b.atmosphere.distanceFogSkyBand, t);
		}

		for (int i = 0; i < 3; ++i)
		{
			state.timeOfDay.fogTint[i] = lerpFloat(a.timeOfDay.fogTint[i], b.timeOfDay.fogTint[i], t);
			state.timeOfDay.sunScatterColor[i] = lerpFloat(a.timeOfDay.sunScatterColor[i], b.timeOfDay.sunScatterColor[i], t);
			state.timeOfDay.distanceFogColor[i] = lerpFloat(a.timeOfDay.distanceFogColor[i], b.timeOfDay.distanceFogColor[i], t);
		}
		state.timeOfDay.distanceFogOpacity = lerpFloat(a.timeOfDay.distanceFogOpacity, b.timeOfDay.distanceFogOpacity, t);
		state.timeOfDay.densityMultiplier = lerpFloat(a.timeOfDay.densityMultiplier, b.timeOfDay.densityMultiplier, t);
		state.timeOfDay.shaftMultiplier = lerpFloat(a.timeOfDay.shaftMultiplier, b.timeOfDay.shaftMultiplier, t);

		state.exposure = lerpFloat(a.exposure, b.exposure, t);
		state.bloomIntensity = lerpFloat(a.bloomIntensity, b.bloomIntensity, t);
		state.bloomThreshold = lerpFloat(a.bloomThreshold, b.bloomThreshold, t);

		// Blend the direction as a vector so 350 -> 10 degrees passes through 0, not 180. Opposite
		// winds cancel out; the incoming direction wins then.
		const Vector3 blendedWind = a.windDirection * (1.0f - t) + b.windDirection * t;
		const float windLength = blendedWind.GetLength();
		state.windDirection = windLength < 1e-3f ? b.windDirection : blendedWind * (1.0f / windLength);
		state.windSpeed = lerpFloat(a.windSpeed, b.windSpeed, t);
		state.windGustiness = lerpFloat(a.windGustiness, b.windGustiness, t);
		state.fogNoiseAmount = lerpFloat(a.fogNoiseAmount, b.fogNoiseAmount, t);
		// The fog shader samples the noise at worldPos / fogNoiseSize, so a size that moves every frame
		// rescales the lookup and sweeps the pattern across the world - many tiles per second far from
		// the origin. The size therefore steps once, at the midpoint of the blend.
		state.fogNoiseSize = t < 0.5f ? a.fogNoiseSize : b.fogNoiseSize;
		state.lightScattering = lerpFloat(a.lightScattering, b.lightScattering, t);

		// LUTs cannot be averaged: this is a plain two-way blend, which is correct for a two-entry
		// lerp. EnvironmentController::Evaluate overwrites these three with the target/strongest-other
		// pair after folding every blended entry, since that fold is not a simple pairwise lerp.
		state.colorLut = b.colorLut;
		state.colorLutFrom = a.colorLut;
		state.colorLutBlend = t;
		state.saturation = lerpFloat(a.saturation, b.saturation, t);
		state.contrast = lerpFloat(a.contrast, b.contrast, t);
		state.colorFilter = a.colorFilter + (b.colorFilter - a.colorFilter) * t;

		return state;
	}
}
