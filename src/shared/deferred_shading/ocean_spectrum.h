// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "ocean_settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace mmo
{
	/// @brief CPU side of the FFT ocean's wave model: the parameters CS_OceanSpectrum.hlsl needs,
	///        plus reference integrals the unit tests use to keep the model physically sensible.
	///        Header-only so deferred_shading_tests can link it without a graphics device.
	namespace ocean_spectrum
	{
		constexpr float Gravity = 9.81f;
		constexpr float Pi = 3.14159265358979f;

		/// @brief Period after which the animation repeats exactly. Long enough not to be noticed.
		constexpr float RepeatPeriod = 240.0f;

		/// @brief Peak enhancement of the JONSWAP spectrum (the standard North Sea value).
		constexpr float JonswapGamma = 3.3f;

		/// @brief Fitted parameters of a fetch-limited JONSWAP wind sea.
		struct JonswapParameters
		{
			/// @brief Phillips constant.
			float alpha;

			/// @brief Angular frequency of the spectral peak (rad/s).
			float peakOmega;
		};

		/// @brief Fits the JONSWAP spectrum to a wind speed (m/s) and fetch (km).
		inline JonswapParameters ComputeJonswap(const float windSpeed, const float fetchKm)
		{
			const float wind = std::max(windSpeed, 0.5f);
			const float fetch = std::max(fetchKm, 0.1f) * 1000.0f;
			JonswapParameters result;
			result.alpha = 0.076f * std::pow(wind * wind / (fetch * Gravity), 0.22f);
			result.peakOmega = 22.0f * std::pow(Gravity * Gravity / (wind * fetch), 1.0f / 3.0f);
			return result;
		}

		/// @brief JONSWAP frequency spectrum S(omega) in m^2 s.
		inline float Jonswap(const float omega, const JonswapParameters& parameters)
		{
			if (omega <= 0.0f)
			{
				return 0.0f;
			}

			const float sigma = omega <= parameters.peakOmega ? 0.07f : 0.09f;
			const float d = (omega - parameters.peakOmega) / (sigma * parameters.peakOmega);
			const float r = std::exp(-0.5f * d * d);
			const float ratio = parameters.peakOmega / omega;
			const float r4 = ratio * ratio * ratio * ratio;
			return parameters.alpha * Gravity * Gravity / std::pow(omega, 5.0f) * std::exp(-1.25f * r4) * std::pow(JonswapGamma, r);
		}

		/// @brief Significant wave height (4 * standard deviation of the surface) of a JONSWAP sea, by
		///        numerical integration. Used to sanity check the defaults.
		inline float SignificantWaveHeight(const float windSpeed, const float fetchKm)
		{
			const JonswapParameters parameters = ComputeJonswap(windSpeed, fetchKm);
			const float start = parameters.peakOmega * 0.3f;
			const float end = parameters.peakOmega * 12.0f;
			constexpr int steps = 4000;
			const float step = (end - start) / static_cast<float>(steps);
			double m0 = 0.0;
			for (int i = 0; i < steps; ++i)
			{
				const float omega = start + (static_cast<float>(i) + 0.5f) * step;
				m0 += static_cast<double>(Jonswap(omega, parameters)) * step;
			}

			return 4.0f * static_cast<float>(std::sqrt(m0));
		}

		/// @brief Normalisation of the directional spreading |cos(theta / 2)|^(2s) so it integrates to
		///        one over the full circle: Gamma(s + 1) / (2 sqrt(pi) Gamma(s + 1/2)).
		inline float SpreadNormalization(const float exponent)
		{
			const float s = std::max(exponent, 0.0f);
			return std::exp(std::lgamma(s + 1.0f) - std::lgamma(s + 0.5f)) / (2.0f * std::sqrt(Pi));
		}

		/// @brief Range of wave numbers |k| one cascade simulates. The ranges of all cascades tile
		///        the spectrum without overlap, so no wave is counted twice.
		struct CascadeBand
		{
			float low;
			float high;
		};

		/// @brief Number of whole waves a patch must hold before it takes over a band from the
		///        larger cascade. Fewer would leave the smaller patch's longest waves too coarse.
		constexpr float CascadeHandoverCycles = 4.0f;

		/// @brief Splits the wave-number axis between cascades of the given sizes (largest first).
		inline std::array<CascadeBand, OceanCascadeCount> ComputeCascadeBands(const std::array<float, OceanCascadeCount>& sizes)
		{
			std::array<CascadeBand, OceanCascadeCount> bands {};
			float low = 0.0f;
			for (uint32 i = 0; i < OceanCascadeCount; ++i)
			{
				bands[i].low = low;
				if (i + 1 < OceanCascadeCount)
				{
					const float handover = 2.0f * Pi / std::max(sizes[i + 1], 0.01f) * CascadeHandoverCycles;
					bands[i].high = std::max(handover, low);
				}
				else
				{
					bands[i].high = std::numeric_limits<float>::max();
				}
				low = bands[i].high;
			}

			return bands;
		}
	}
}
