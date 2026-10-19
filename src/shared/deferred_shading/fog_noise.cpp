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

		/// Void-and-cluster state over a toroidal size x size grid: which texels hold a point and each texel's
		/// Gaussian-weighted closeness to all points (high = cluster, low = void).
		class BlueNoiseField
		{
		public:
			explicit BlueNoiseField(const uint32 size)
				: m_size(size)
				, m_kernel(static_cast<size_t>(size) * size)
				, m_points(static_cast<size_t>(size) * size, 0)
				, m_energy(static_cast<size_t>(size) * size, 0.0f)
			{
				constexpr float sigma = 1.5f;
				for (uint32 y = 0; y < size; ++y)
				{
					for (uint32 x = 0; x < size; ++x)
					{
						const float dx = static_cast<float>(std::min(x, size - x));
						const float dy = static_cast<float>(std::min(y, size - y));
						m_kernel[static_cast<size_t>(y) * size + x] = std::exp(-(dx * dx + dy * dy) / (2.0f * sigma * sigma));
					}
				}
			}

			[[nodiscard]] bool HasPoint(const size_t index) const { return m_points[index] != 0; }

			void Set(const size_t index, const bool point)
			{
				if (HasPoint(index) == point)
				{
					return;
				}

				m_points[index] = point ? 1 : 0;
				const float sign = point ? 1.0f : -1.0f;
				const uint32 px = static_cast<uint32>(index % m_size);
				const uint32 py = static_cast<uint32>(index / m_size);
				for (uint32 y = 0; y < m_size; ++y)
				{
					const size_t kernelRow = static_cast<size_t>((y + m_size - py) % m_size) * m_size;
					for (uint32 x = 0; x < m_size; ++x)
					{
						m_energy[static_cast<size_t>(y) * m_size + x] += sign * m_kernel[kernelRow + (x + m_size - px) % m_size];
					}
				}
			}

			/// The point with the most energy: the centre of the tightest cluster.
			[[nodiscard]] size_t TightestCluster() const
			{
				size_t best = 0;
				float bestEnergy = -1.0f;
				for (size_t i = 0; i < m_points.size(); ++i)
				{
					if (m_points[i] != 0 && m_energy[i] > bestEnergy)
					{
						bestEnergy = m_energy[i];
						best = i;
					}
				}

				return best;
			}

			/// The empty texel with the least energy: the centre of the largest void.
			[[nodiscard]] size_t LargestVoid() const
			{
				size_t best = 0;
				float bestEnergy = 0.0f;
				bool found = false;
				for (size_t i = 0; i < m_points.size(); ++i)
				{
					if (m_points[i] == 0 && (!found || m_energy[i] < bestEnergy))
					{
						bestEnergy = m_energy[i];
						best = i;
						found = true;
					}
				}

				return best;
			}

		private:
			uint32 m_size;
			std::vector<float> m_kernel;
			std::vector<uint8> m_points;
			std::vector<float> m_energy;
		};
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

		std::vector<float> normalized(texelCount, 0.0f);
		double normalizedSum = 0.0;
		for (size_t i = 0; i < texelCount; ++i)
		{
			normalized[i] = std::clamp((values[i] - minimum) / span, 0.0f, 1.0f);
			normalizedSum += normalized[i];
		}

		// Min/max normalization still leaves the mean below 0.5 (a value-noise fractal sum skews low),
		// so NoiseDensityFactor's average density comes out under 1 and the fog dims slightly at the
		// grid's range. Remap piecewise around the measured mean m so it lands on 0.5 while keeping the
		// endpoints (and therefore the full [0, 255] range) fixed.
		const float mean = static_cast<float>(normalizedSum / static_cast<double>(texelCount));

		std::vector<uint8> texels(texelCount, 0);
		if (mean > 0.0f && mean < 1.0f)
		{
			for (size_t i = 0; i < texelCount; ++i)
			{
				const float value = normalized[i];
				const float remapped = value <= mean
					? value * 0.5f / mean
					: 0.5f + (value - mean) * 0.5f / (1.0f - mean);
				texels[i] = static_cast<uint8>(std::lround(std::clamp(remapped, 0.0f, 1.0f) * 255.0f));
			}
		}
		else
		{
			// Degenerate mean (all-zero or all-one input): nothing to recentre, use the plain normalized value.
			for (size_t i = 0; i < texelCount; ++i)
			{
				texels[i] = static_cast<uint8>(std::lround(normalized[i] * 255.0f));
			}
		}

		return texels;
	}

	std::vector<uint8> GenerateBlueNoise(const uint32 size, const uint32 seed)
	{
		const size_t texelCount = static_cast<size_t>(size) * size;
		if (texelCount == 0)
		{
			return {};
		}

		// 1. A random tenth of the texels as the initial points.
		BlueNoiseField initial(size);
		const size_t initialCount = std::max<size_t>(1, texelCount / 10);
		size_t placed = 0;
		for (uint32 attempt = 0; placed < initialCount; ++attempt)
		{
			const size_t index = hashLattice(attempt, 0, 0, 0xB1u, seed) % texelCount;
			if (!initial.HasPoint(index))
			{
				initial.Set(index, true);
				++placed;
			}
		}

		// 2. Relax them: move the point of the tightest cluster into the largest void until it would land
		//    where it came from.
		for (size_t iteration = 0; iteration < texelCount; ++iteration)
		{
			const size_t cluster = initial.TightestCluster();
			initial.Set(cluster, false);
			const size_t emptiest = initial.LargestVoid();
			initial.Set(emptiest, true);
			if (emptiest == cluster)
			{
				break;
			}
		}

		std::vector<uint32> rank(texelCount, 0);

		// 3. Rank the initial points: the tightest cluster is removed first and gets the highest rank below
		//    the initial count.
		BlueNoiseField removal = initial;
		for (size_t count = initialCount; count > 0; --count)
		{
			const size_t cluster = removal.TightestCluster();
			removal.Set(cluster, false);
			rank[cluster] = static_cast<uint32>(count - 1);
		}

		// 4. Rank the rest by filling the largest void each time.
		BlueNoiseField insertion = initial;
		for (size_t count = initialCount; count < texelCount; ++count)
		{
			const size_t emptiest = insertion.LargestVoid();
			insertion.Set(emptiest, true);
			rank[emptiest] = static_cast<uint32>(count);
		}

		std::vector<uint8> texels(texelCount, 0);
		for (size_t i = 0; i < texelCount; ++i)
		{
			texels[i] = static_cast<uint8>((static_cast<uint64>(rank[i]) * 256u) / texelCount);
		}

		return texels;
	}
}
