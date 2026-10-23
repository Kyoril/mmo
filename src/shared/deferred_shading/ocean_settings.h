// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <array>

namespace mmo
{
	/// @brief Number of FFT wave cascades the ocean simulates. Each one is an N x N patch of its own
	///        size; together they cover swell, chop and ripples without visible tiling.
	constexpr uint32 OceanCascadeCount = 3;

	/// @brief Resolution of every FFT cascade.
	constexpr uint32 OceanFftResolution = 256;

	/// @brief Range of the water depth the terrain ocean mask encodes in its green channel (metres).
	///        Mirrors terrain::ocean_mask::DepthRange; OceanCommon.hlsli decodes with the same value.
	constexpr float OceanMaskDepthRange = 32.0f;

	/// @brief Everything that shapes and shades the FFT ocean. Authored per liquid type in the water
	///        profile (proto WaterProfile), converted with ToOceanSettings, and handed to
	///        DeferredRenderer::SetOceanSettings. The defaults are the look the engine ships with.
	struct OceanSettings
	{
		// --- Waves -----------------------------------------------------------------------------

		/// @brief Wind speed 10 m above the sea in m/s. Drives wave height and wavelength (JONSWAP).
		float windSpeed { 10.5f };

		/// @brief Direction the wind blows toward, in degrees. 0 = +X, 90 = +Z.
		float windDirection { 35.0f };

		/// @brief Distance over which the wind has been blowing, in kilometres. Short fetch = young,
		///        choppy sea; long fetch = developed swell.
		float fetch { 180.0f };

		/// @brief Directional spreading exponent. Low = confused sea from every direction, high =
		///        long-crested swell lined up with the wind.
		float spread { 6.0f };

		/// @brief Multiplier on the physical wave amplitude. 1 = what the spectrum says.
		float waveScale { 1.2f };

		/// @brief Horizontal displacement factor. Higher pinches crests and opens troughs.
		float choppiness { 1.35f };

		/// @brief Multiplier on the simulated time, i.e. wave speed.
		float timeScale { 1.0f };

		/// @brief Patch size of each cascade in metres, largest first. Values are chosen not to share
		///        a common factor, so the patches never repeat together.
		std::array<float, OceanCascadeCount> cascadeSizes { 307.0f, 71.0f, 13.0f };

		/// @brief Amplitude multiplier per cascade. Turning the finer cascades down keeps the swell
		///        dominant and the surface smooth, the stylised look the engine wants.
		std::array<float, OceanCascadeCount> cascadeWeights { 1.0f, 0.85f, 0.5f };

		/// @brief Waves much shorter than this (m) are damped out of the spectrum.
		float shortWaveLength { 0.08f };

		/// @brief Water depth (m) below which the longest waves start to flatten toward the shore.
		float shallowDepth { 6.0f };

		// --- Optics ----------------------------------------------------------------------------

		/// @brief Beer-Lambert absorption per metre and channel (linear). Red dies first.
		Vector3 absorption { 0.42f, 0.11f, 0.075f };

		/// @brief Scattering albedo in wave troughs (linear).
		Vector3 troughColor { 0.065f, 0.402f, 0.434f };

		/// @brief Scattering albedo at wave crests, where light passes through thin water (linear).
		Vector3 crestColor { 0.451f, 0.708f, 0.565f };

		/// @brief Strength of sunlight scattered through wave crests toward the viewer.
		float subsurfaceStrength { 4.0f };

		/// @brief Forward-scattering anisotropy of the subsurface glow (Henyey-Greenstein g).
		float subsurfaceAnisotropy { 0.75f };

		/// @brief Strength of the isotropic in-scattering of sun and sky light (the body colour).
		float ambientScatter { 1.2f };

		/// @brief Base surface roughness for reflections and the sun highlight.
		float roughness { 0.12f };

		/// @brief Multiplier on reflected sky and scene light.
		float reflectionStrength { 0.75f };

		/// @brief Multiplier on the sun highlight.
		float sunSpecularStrength { 1.0f };

		/// @brief Screen-space refraction strength.
		float refractionStrength { 0.06f };

		// --- Foam ------------------------------------------------------------------------------

		/// @brief How readily folding crests turn white (Jacobian threshold offset). Higher = more foam.
		float foamCoverage { 0.21f };

		/// @brief Rate at which foam builds up on breaking crests, per second.
		float foamGrowth { 12.0f };

		/// @brief Rate at which foam dissolves, per second.
		float foamDecay { 0.9f };

		/// @brief Brightness of the foam (linear albedo).
		float foamBrightness { 1.0f };

		/// @brief Strength of the foam band along the shore.
		float shoreFoamStrength { 1.0f };

		/// @brief Distance from the shore (m) over which the rolling shore foam bands fade out.
		float shoreFoamWidth { 14.0f };
	};

	/// @brief The terrain-side input of the ocean renderer: where the sea is and how deep it is.
	struct OceanSurfaceState
	{
		/// @brief Whether the ocean is drawn at all.
		bool enabled { false };

		/// @brief Height of the undisturbed sea surface.
		float seaLevel { 0.0f };

		/// @brief World X of the mask window's minimum corner.
		float maskOriginX { 0.0f };

		/// @brief World Z of the mask window's minimum corner.
		float maskOriginZ { 0.0f };

		/// @brief World size of one mask texel.
		float maskTexelSize { 1.0f };
	};
}
