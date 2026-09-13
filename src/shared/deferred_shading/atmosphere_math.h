// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <algorithm>
#include <cmath>

namespace mmo::atmosphere
{
	/// @brief Largest display value InverseTonemap inverts. ACES reaches 1.0 at a finite input,
	///        so 1.0 itself has no stable inverse.
	constexpr float MaxInvertibleDisplay = 0.999f;

	/// @brief Upper bound of the height-fog density exponent. Keeps rays that reach far below the
	///        base height from overflowing floating point.
	constexpr float MaxDensityExponent = 12.0f;

	/// @brief Display gamma used by the tonemap pass and the forward materials.
	constexpr float Gamma = 2.2f;

	/// @brief Narkowicz ACES fit, identical to ACESFilm in the HLSL shaders.
	inline float AcesFilm(const float x)
	{
		constexpr float a = 2.51f;
		constexpr float b = 0.03f;
		constexpr float c = 2.43f;
		constexpr float d = 0.59f;
		constexpr float e = 0.14f;
		return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
	}

	/// @brief Linear HDR to display value: ACES then gamma.
	inline float Tonemap(const float linear)
	{
		return std::pow(AcesFilm(linear), 1.0f / Gamma);
	}

	/// @brief Analytic inverse of AcesFilm for y in [0, a/c).
	/// @remark Solves (y·c − a)x² + (y·d − b)x + y·e = 0. The leading coefficient is negative and
	///         the constant term non-negative, so exactly one root is non-negative.
	inline float InverseAcesFilm(const float y)
	{
		constexpr float a = 2.51f;
		constexpr float b = 0.03f;
		constexpr float c = 2.43f;
		constexpr float d = 0.59f;
		constexpr float e = 0.14f;
		const float qa = y * c - a;
		const float qb = y * d - b;
		const float qc = y * e;
		return (-qb - std::sqrt(std::max(qb * qb - 4.0f * qa * qc, 0.0f))) / (2.0f * qa);
	}

	/// @brief Display value to the linear HDR value Tonemap maps back onto it.
	inline float InverseTonemap(const float display)
	{
		const float clamped = std::clamp(display, 0.0f, MaxInvertibleDisplay);
		return InverseAcesFilm(std::pow(clamped, Gamma));
	}

	/// @brief Extinction coefficient of the exponential height fog at world height y.
	inline float FogDensityAt(const float y, const float density, const float falloff, const float baseHeight)
	{
		return density * std::exp(std::min(-falloff * (y - baseHeight), MaxDensityExponent));
	}

	/// @brief Closed-form optical depth of the height fog along a straight ray segment.
	/// @param originY World height of the segment start.
	/// @param dirY Y component of the normalised ray direction.
	/// @param length Segment length in metres.
	/// @remark With σ(t) = σ_start·e^(−k·t/L) and k = falloff·dirY·L, the integral is
	///         L·(σ_start − σ_end)/k. Near k = 0 the trapezoid rule is used instead, which is exact
	///         to O(k²) and avoids the cancellation in the difference.
	inline float FogOpticalDepth(const float originY, const float dirY, const float length,
		const float density, const float falloff, const float baseHeight)
	{
		const float sigmaStart = FogDensityAt(originY, density, falloff, baseHeight);
		const float sigmaEnd = FogDensityAt(originY + dirY * length, density, falloff, baseHeight);
		const float k = falloff * dirY * length;
		if (std::abs(k) < 1e-3f)
		{
			return length * 0.5f * (sigmaStart + sigmaEnd);
		}

		return length * (sigmaStart - sigmaEnd) / k;
	}

	/// @brief Scattering phase normalised so an isotropic medium yields 1: a Henyey-Greenstein lobe
	///        blended 80/20 with the isotropic term.
	/// @param anisotropy Henyey-Greenstein g in [0, 1).
	/// @param cosTheta Cosine between the view ray and the direction toward the sun.
	inline float ScatterPhase(const float anisotropy, const float cosTheta)
	{
		const float g2 = anisotropy * anisotropy;
		const float hg = (1.0f - g2) / std::pow(std::max(1.0f + g2 - 2.0f * anisotropy * cosTheta, 1e-4f), 1.5f);
		return 1.0f + (hg - 1.0f) * 0.8f;
	}
}
