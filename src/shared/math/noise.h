// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cmath>
#include <array>

namespace mmo
{
	namespace noise
	{
		namespace detail
		{
			// Ken Perlin's permutation table (doubled to avoid overflow)
			static constexpr std::array<int, 512> Perm = []() constexpr
			{
				std::array<int, 256> p = {
					151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
					140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
					247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
					 57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
					 74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
					 60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
					 65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
					200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
					 52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
					207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
					119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
					129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
					218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
					 81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
					184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
					222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
				};
				std::array<int, 512> doubled = {};
				for (int i = 0; i < 256; ++i)
				{
					doubled[i] = p[i];
					doubled[i + 256] = p[i];
				}
				return doubled;
			}();

			inline float Fade(float t) noexcept
			{
				return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
			}

			inline float Lerp(float a, float b, float t) noexcept
			{
				return a + t * (b - a);
			}

			inline float Grad(int hash, float x, float z) noexcept
			{
				// 4 gradient directions in 2D
				switch (hash & 3)
				{
					case 0: return  x + z;
					case 1: return -x + z;
					case 2: return  x - z;
					case 3: return -x - z;
					default: return 0.0f;
				}
			}
		} // namespace detail

		/// @brief Classic 2D Perlin gradient noise.
		/// @param x X coordinate.
		/// @param z Z coordinate.
		/// @return Noise value in approximately [-1, 1].
		inline float Perlin(float x, float z) noexcept
		{
			// Integer cell coordinates
			const int ix = static_cast<int>(std::floor(x)) & 255;
			const int iz = static_cast<int>(std::floor(z)) & 255;

			// Fractional offsets within cell
			const float fx = x - std::floor(x);
			const float fz = z - std::floor(z);

			// Smooth fade curves
			const float u = detail::Fade(fx);
			const float w = detail::Fade(fz);

			// Hash corner coordinates
			const int aa = detail::Perm[detail::Perm[ix    ] + iz    ];
			const int ba = detail::Perm[detail::Perm[ix + 1] + iz    ];
			const int ab = detail::Perm[detail::Perm[ix    ] + iz + 1];
			const int bb = detail::Perm[detail::Perm[ix + 1] + iz + 1];

			// Interpolate gradients
			const float x1 = detail::Lerp(detail::Grad(aa, fx,       fz      ),
			                               detail::Grad(ba, fx - 1.0f, fz      ), u);
			const float x2 = detail::Lerp(detail::Grad(ab, fx,       fz - 1.0f),
			                               detail::Grad(bb, fx - 1.0f, fz - 1.0f), u);
			return detail::Lerp(x1, x2, w);
		}

		/// @brief Fractional Brownian Motion — sums multiple octaves of Perlin noise.
		/// @param x X coordinate.
		/// @param z Z coordinate.
		/// @param octaves Number of noise layers.
		/// @param persistence Amplitude multiplier per octave (typical: 0.5).
		/// @return Accumulated noise value.
		inline float fBm(float x, float z, int octaves, float persistence) noexcept
		{
			if (octaves <= 0)
			{
				return 0.0f;
			}

			float value     = 0.0f;
			float amplitude = 1.0f;
			float frequency = 1.0f;

			for (int i = 0; i < octaves; ++i)
			{
				value     += amplitude * Perlin(x * frequency, z * frequency);
				amplitude *= persistence;
				frequency *= 2.0f;
			}

			return value;
		}

	} // namespace noise
} // namespace mmo
