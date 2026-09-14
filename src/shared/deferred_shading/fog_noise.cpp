// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "fog_noise.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr uint32 OctaveCount = 4;
		constexpr uint32 BasePeriod = 4;

		uint32 hashLattice(const uint32 x, const uint32 y, const uint32 z, const uint32 octave, const uint32 seed)
		{
			uint32 h = seed * 0x9E3779B1u + octave * 0x632BE5ABu;
			h ^= x * 0x85EBCA6Bu;
			h = (h << 13) | (h >> 19);
			h ^= y * 0xC2B2AE35u;
			h = (h << 13) | (h >> 19);
			h ^= z * 0x27D4EB2Fu;
			h ^= h >> 16;
			h *= 0x7FEB352Du;
			h ^= h >> 15;
			h *= 0x846CA68Bu;
			h ^= h >> 16;
			return h;
		}

		float latticeValue(const uint32 x, const uint32 y, const uint32 z, const uint32 octave, const uint32 seed)
		{
			return static_cast<float>(hashLattice(x, y, z, octave, seed) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
		}

		float fade(const float t)
		{
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}

		/// Value noise on a lattice that wraps every `period` cells, sampled at lattice coordinates.
		float periodicValueNoise(const float u, const float v, const float w, const uint32 period, const uint32 octave, const uint32 seed)
		{
			const float fu = std::floor(u);
			const float fv = std::floor(v);
			const float fw = std::floor(w);

			const uint32 x0 = static_cast<uint32>(fu) % period;
			const uint32 y0 = static_cast<uint32>(fv) % period;
			const uint32 z0 = static_cast<uint32>(fw) % period;
			const uint32 x1 = (x0 + 1) % period;
			const uint32 y1 = (y0 + 1) % period;
			const uint32 z1 = (z0 + 1) % period;

			const float tx = fade(u - fu);
			const float ty = fade(v - fv);
			const float tz = fade(w - fw);

			const auto lerp = [](const float a, const float b, const float t) { return a + (b - a) * t; };

			const float c00 = lerp(latticeValue(x0, y0, z0, octave, seed), latticeValue(x1, y0, z0, octave, seed), tx);
			const float c10 = lerp(latticeValue(x0, y1, z0, octave, seed), latticeValue(x1, y1, z0, octave, seed), tx);
			const float c01 = lerp(latticeValue(x0, y0, z1, octave, seed), latticeValue(x1, y0, z1, octave, seed), tx);
			const float c11 = lerp(latticeValue(x0, y1, z1, octave, seed), latticeValue(x1, y1, z1, octave, seed), tx);

			return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
		}
	}

	std::vector<uint8> GenerateFogNoise(const uint32 size, const uint32 seed)
	{
		const size_t texelCount = static_cast<size_t>(size) * size * size;
		std::vector<float> values(texelCount, 0.0f);

		float minimum = 1.0f;
		float maximum = 0.0f;

		for (uint32 z = 0; z < size; ++z)
		{
			for (uint32 y = 0; y < size; ++y)
			{
				for (uint32 x = 0; x < size; ++x)
				{
					float sum = 0.0f;
					float amplitude = 1.0f;
					float amplitudeSum = 0.0f;
					uint32 period = BasePeriod;

					for (uint32 octave = 0; octave < OctaveCount; ++octave)
					{
						// Texel coordinate scaled so texel `size` lands exactly on lattice cell `period`,
						// which wraps to 0: the volume tiles.
						const float scale = static_cast<float>(period) / static_cast<float>(size);
						sum += amplitude * periodicValueNoise(static_cast<float>(x) * scale, static_cast<float>(y) * scale, static_cast<float>(z) * scale, period, octave, seed);
						amplitudeSum += amplitude;
						amplitude *= 0.5f;
						period *= 2;
					}

					const float value = sum / amplitudeSum;
					values[static_cast<size_t>(z) * size * size + static_cast<size_t>(y) * size + x] = value;
					minimum = std::min(minimum, value);
					maximum = std::max(maximum, value);
				}
			}
		}

		// Fractal sums crowd around 0.5; stretching to the full range gives the fog visible gaps.
		const float span = std::max(maximum - minimum, 1e-6f);

		std::vector<uint8> texels(texelCount, 0);
		for (size_t i = 0; i < texelCount; ++i)
		{
			const float normalized = (values[i] - minimum) / span;
			texels[i] = static_cast<uint8>(std::lround(std::clamp(normalized, 0.0f, 1.0f) * 255.0f));
		}

		return texels;
	}
}
