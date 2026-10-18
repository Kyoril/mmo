// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief User-tunable base values of the height fog, sourced from the active zone's
	/// environment profile.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct AtmosphereParameters
	{
		/// @brief Extinction per metre at BaseHeight.
		float density = 0.0015f;

		/// @brief Exponential falloff of the density per metre of height above BaseHeight.
		float heightFalloff = 0.05f;

		/// @brief World Y of the fog base: the height at which the density equals `density`.
		/// @remark Absolute, so a fog bank stays where it was authored and climbing above it leaves it
		///         below you. Zones whose terrain sits far from Y = 0 must author their own base.
		float baseHeight = 0.0f;

		/// @brief Henyey-Greenstein g of the sun scattering lobe. Larger = tighter sun glow.
		float anisotropy = 0.7f;

		/// @brief Multiplier on the sun's in-scattered light (the shafts and the sun glow).
		/// @remark Scattered light grows with density, so this stays above 1 to keep shafts readable
		///         in the thin default fog, but low enough that the tonemapper keeps their tint.
		float shaftStrength = 1.25f;

		/// @brief Distance from the camera, in metres, where the distance fog starts.
		float distanceFogStart = 0.0f;

		/// @brief Distance from the camera, in metres, where the distance fog reaches its full opacity.
		/// @remark Distance fog is off while this is not greater than distanceFogStart.
		float distanceFogEnd = 0.0f;

		/// @brief How far up the sky the distance fog reaches, as the Y component of the view ray
		///        (0.2 ~ 11.5 degrees above the horizon). 0 leaves the sky untouched.
		float distanceFogSkyBand = 0.2f;

		/// @brief Sets the base density, clamped to [0, 1].
		void SetDensity(const float value) { density = Clamp(value, 0.0f, 1.0f); }

		/// @brief Sets the height falloff, clamped to [0, 1].
		void SetHeightFalloff(const float value) { heightFalloff = Clamp(value, 0.0f, 1.0f); }

		/// @brief Sets the base height, clamped to [-10000, 10000].
		void SetBaseHeight(const float value) { baseHeight = Clamp(value, -10000.0f, 10000.0f); }

		/// @brief Sets the anisotropy, clamped to [0, 0.95]; g >= 1 is singular.
		void SetAnisotropy(const float value) { anisotropy = Clamp(value, 0.0f, 0.95f); }

		/// @brief Sets the shaft strength, clamped to [0, 16].
		void SetShaftStrength(const float value) { shaftStrength = Clamp(value, 0.0f, 16.0f); }

		/// @brief Sets the distance fog start, clamped to [0, 100000].
		void SetDistanceFogStart(const float value) { distanceFogStart = Clamp(value, 0.0f, 100000.0f); }

		/// @brief Sets the distance fog end, clamped to [0, 100000].
		void SetDistanceFogEnd(const float value) { distanceFogEnd = Clamp(value, 0.0f, 100000.0f); }

		/// @brief Sets the distance fog sky band, clamped to [0, 1].
		void SetDistanceFogSkyBand(const float value) { distanceFogSkyBand = Clamp(value, 0.0f, 1.0f); }

		/// @brief Whether the start/end pair describes an active distance fog.
		[[nodiscard]] bool HasDistanceFog() const { return distanceFogEnd > distanceFogStart; }

	private:
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

	/// @brief Values the time-of-day curves produce for the current hour. Written by SkyComponent.
	struct AtmosphereTimeOfDay
	{
		/// @brief Ambient radiance of the fog, linear RGB. Defaults to the legacy fog colour.
		float fogTint[3]{ 0.447f, 0.638f, 1.0f };

		/// @brief Multiplier on AtmosphereParameters::density.
		float densityMultiplier = 1.0f;

		/// @brief Tint of the sun light inside the fog, linear RGB.
		float sunScatterColor[3]{ 1.0f, 1.0f, 1.0f };

		/// @brief Multiplier on AtmosphereParameters::shaftStrength.
		float shaftMultiplier = 1.0f;

		/// @brief Colour distant geometry fades into, linear RGB.
		float distanceFogColor[3]{ 0.5f, 0.7f, 1.0f };

		/// @brief Opacity of the distance fog at and beyond its end distance, [0, 1].
		float distanceFogOpacity = 1.0f;
	};

	/// @brief The final per-frame fog values uploaded to the camera constant buffer.
	struct AtmosphereConstants
	{
		float density = 0.0f;
		float heightFalloff = 0.0f;
		float baseHeight = 0.0f;
		float anisotropy = 0.0f;
		float fogTint[3]{ 0.0f, 0.0f, 0.0f };
		float sunScatterColor[3]{ 0.0f, 0.0f, 0.0f };
		float shaftStrength = 0.0f;

		/// @brief Distance fog colour, linear RGB.
		float distanceFogColor[3]{ 0.0f, 0.0f, 0.0f };

		/// @brief Distance fog opacity at its end distance; 0 = distance fog off.
		float distanceFogOpacity = 0.0f;

		/// @brief Distance where the distance fog starts.
		float distanceFogStart = 0.0f;

		/// @brief 1 / (end - start); 0 while the distance fog is off.
		float distanceFogInverseRange = 0.0f;

		/// @brief View-ray Y up to which the sky receives distance fog; 0 = sky untouched.
		float distanceFogSkyBand = 0.0f;
	};

	/// @brief Combines base parameters with the time-of-day values.
	/// @param parameters Base values from the active zone's environment profile.
	/// @param timeOfDay Curve values for the current hour.
	/// @param fogEnabled When false, density and distance fog opacity are zero, which makes every fog
	///        term vanish.
	[[nodiscard]] inline AtmosphereConstants CombineAtmosphere(const AtmosphereParameters& parameters,
		const AtmosphereTimeOfDay& timeOfDay, const bool fogEnabled)
	{
		const auto nonNegative = [](const float value) { return value < 0.0f ? 0.0f : value; };

		AtmosphereConstants constants;
		constants.density = fogEnabled ? parameters.density * nonNegative(timeOfDay.densityMultiplier) : 0.0f;
		constants.heightFalloff = parameters.heightFalloff;
		constants.baseHeight = parameters.baseHeight;
		constants.anisotropy = parameters.anisotropy;
		constants.shaftStrength = parameters.shaftStrength * nonNegative(timeOfDay.shaftMultiplier);

		for (int i = 0; i < 3; ++i)
		{
			constants.fogTint[i] = nonNegative(timeOfDay.fogTint[i]);
			constants.sunScatterColor[i] = nonNegative(timeOfDay.sunScatterColor[i]);
			constants.distanceFogColor[i] = nonNegative(timeOfDay.distanceFogColor[i]);
		}

		if (fogEnabled && parameters.HasDistanceFog())
		{
			const float opacity = timeOfDay.distanceFogOpacity;
			constants.distanceFogOpacity = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
			constants.distanceFogStart = parameters.distanceFogStart;
			constants.distanceFogInverseRange = 1.0f / (parameters.distanceFogEnd - parameters.distanceFogStart);
			constants.distanceFogSkyBand = parameters.distanceFogSkyBand;
		}

		return constants;
	}
}
