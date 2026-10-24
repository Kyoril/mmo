// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "oasis.h"

#include "pcg_random.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg
{
	namespace
	{
		float saturate(float v)
		{
			return std::min(1.0f, std::max(0.0f, v));
		}

		float smoothstep(float t)
		{
			t = saturate(t);
			return t * t * (3.0f - 2.0f * t);
		}

		/// Rim to floor profile in [0,1]: smoothstep blended with a terraced ramp.
		float Profile(const OasisParams& p, float t)
		{
			const float smooth = smoothstep(t);
			if (p.terraceSteps == 0)
			{
				return smooth;
			}

			const float n = static_cast<float>(p.terraceSteps);
			const float u = t * n;
			const float i = std::floor(u);
			const float f = u - i;
			const float width = 1.0f + (0.1f - 1.0f) * p.terraceSharpness;
			const float step = smoothstep(saturate((f - 0.5f) / width + 0.5f));
			const float terrace = (i + step) / n;
			return smooth + (terrace - smooth) * p.terraceSharpness;
		}

		/// Weight of the oasis at signed distance d: 1 inside, smooth fade outside.
		float InfluenceWeight(const OasisParams& p, float d)
		{
			if (d <= 0.0f)
			{
				return 1.0f;
			}
			return smoothstep(1.0f - saturate(d / p.outerFalloff));
		}
	}

	OasisError PrepareOasis(const PathSpline& loop, uint64 seed, const OasisParams& params,
		const std::function<float(float, float)>& baseHeight, OasisShape& out)
	{
		if (loop.points.size() < 3)
		{
			return OasisError::TooFewPoints;
		}

		PathSpline closedLoop = loop;
		closedLoop.closed = true;
		auto path = std::make_shared<const SampledPath>(closedLoop, 1.0f);

		if (path->IsSelfIntersecting())
		{
			return OasisError::SelfIntersecting;
		}

		// Shoelace area of the sampled polygon.
		const auto& samples = path->GetSamples();
		double area = 0.0;
		for (size_t i = 0; i < samples.size(); ++i)
		{
			const SplineSample& a = samples[i];
			const SplineSample& b = samples[(i + 1) % samples.size()];
			area += static_cast<double>(a.x) * b.z - static_cast<double>(b.x) * a.z;
		}
		if (std::abs(area) * 0.5 < 1.0)
		{
			return OasisError::ZeroArea;
		}

		// Base heights along the loop, 1 m apart.
		std::vector<float> heights;
		const float length = path->GetLength();
		for (float d = 0.0f; d < length; d += 1.0f)
		{
			const SplineSample s = path->SampleAtDistance(d);
			heights.push_back(baseHeight(s.x, s.z));
		}
		if (heights.empty())
		{
			return OasisError::ZeroArea;
		}
		std::sort(heights.begin(), heights.end());
		const size_t index = static_cast<size_t>(saturate(params.rimPercentile) * static_cast<float>(heights.size() - 1));

		out.params = params;
		out.seed = seed;
		out.path = path;
		out.rimHeight = heights[index];
		out.floorHeight = out.rimHeight - params.basinDepth;
		out.waterLevel = out.floorHeight + params.waterDepth;

		float minX, minZ, maxX, maxZ;
		path->GetBounds(minX, minZ, maxX, maxZ);
		const float margin = params.outerFalloff + 2.0f;
		out.influence = WorldRect{ minX - margin, minZ - margin, maxX + margin, maxZ + margin };
		return OasisError::None;
	}

	WorldRect GetInfluenceRect(const OasisShape& shape)
	{
		return shape.influence;
	}

	float GetWaterLevel(const OasisShape& shape)
	{
		return shape.waterLevel;
	}

	TerrainTarget EvaluateTerrain(const OasisShape& shape, float x, float z, float /*baseHeight*/)
	{
		const OasisParams& p = shape.params;
		TerrainTarget target;
		if (!shape.path)
		{
			return target;
		}

		const float d = shape.path->SignedDistance(x, z);
		const float weight = InfluenceWeight(p, d);
		if (weight <= 0.0f)
		{
			return target;
		}

		// Height.
		target.heightWeight = weight;
		if (d < 0.0f)
		{
			const float t = saturate(-d / p.rimWidth);
			const float profile = Profile(p, t);
			const float noise = FractalNoise(shape.seed + 101, x, z, 1.0f / 8.0f, 3, 0.5f) * p.floorNoiseAmplitude;
			target.height = shape.rimHeight + (shape.floorHeight - shape.rimHeight) * profile + noise * t;
		}
		else
		{
			target.height = shape.rimHeight;
		}

		// Water.
		if (d < -p.waterInset)
		{
			target.waterType = 1;
			target.waterHeight = shape.waterLevel;
		}

		// Splat bands: layer coordinate 0 (sand) .. 3 (grass), measured inward from the loop.
		const float s = -d + FractalNoise(shape.seed + 202, x, z, 1.0f / 12.0f, 3, 0.5f) * p.bandNoiseAmplitude;
		constexpr float h = 1.5f;
		const float t1 = -2.0f - p.dirtBand;
		const float t2 = -2.0f;
		const float t3 = p.grassBand;
		const float c = smoothstep((s - t1 + h) / (2.0f * h)) + smoothstep((s - t2 + h) / (2.0f * h))
			+ smoothstep((s - t3 + h) / (2.0f * h));
		const int lower = std::min(2, static_cast<int>(std::floor(c)));
		const float frac = saturate(c - static_cast<float>(lower));
		const int upperWeight = static_cast<int>(std::lround(255.0f * frac));
		target.splat[lower] = static_cast<uint8>(255 - upperWeight);
		target.splat[lower + 1] = static_cast<uint8>(upperWeight);
		target.splatWeight = weight;
		target.setMaterial = true;
		return target;
	}
}
