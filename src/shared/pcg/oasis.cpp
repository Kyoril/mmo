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

		/// The land floor around the pool stays this far above the water surface (more than the
		/// floor noise), so the shore and the vegetation on it are dry.
		constexpr float kShoreFreeboard = 0.6f;
		/// Distance over which the ground drops from the land floor to the pool bed, from the pool edge inward.
		constexpr float kPoolShoreWidth = 5.0f;
		/// Water quads reach this far outward past the pool edge. Their blocky outline then lies under
		/// the dry shore, and the terrain draws the shoreline.
		constexpr float kWaterQuadMargin = 4.0f;

		constexpr uint64 kRuleRim = 1;
		constexpr uint64 kRulePalm = 2;
		constexpr uint64 kRuleBush = 3;
		constexpr uint64 kRulePlant = 4;
		constexpr float kPi = 3.14159265358979f;

		/// Sub-hash of a rule, so different rules never share a stream.
		uint64 RuleSeed(uint64 seed, uint64 rule)
		{
			return Hash(seed, static_cast<int64>(rule), 0x0A515LL);
		}

		/// Picks a non-empty rock family by weight; null if none is usable.
		const std::vector<String>* PickFamily(float u, const OasisParams& p, const OasisAssets& a)
		{
			const std::vector<String>* lists[3] = { &a.cliffs, &a.shelves, &a.clumps };
			const float weights[3] = { p.cliffWeight, p.shelfWeight, p.clumpWeight };
			float total = 0.0f;
			for (int i = 0; i < 3; ++i)
			{
				if (!lists[i]->empty() && weights[i] > 0.0f)
				{
					total += weights[i];
				}
			}
			if (total <= 0.0f)
			{
				return nullptr;
			}

			float target = u * total;
			const std::vector<String>* last = nullptr;
			for (int i = 0; i < 3; ++i)
			{
				if (lists[i]->empty() || weights[i] <= 0.0f)
				{
					continue;
				}
				last = lists[i];
				if (target < weights[i])
				{
					return lists[i];
				}
				target -= weights[i];
			}
			return last;
		}

		const String& PickAsset(float u, const std::vector<String>& list)
		{
			const size_t index = std::min(list.size() - 1, static_cast<size_t>(u * static_cast<float>(list.size())));
			return list[index];
		}

		Quaternion YawRotation(float yawRadians)
		{
			return Quaternion(Radian(yawRadians), Vector3::UnitY);
		}
	}

	void SanitizeParams(OasisParams& params)
	{
		// Same ranges as the rows of the Procedural mode details panel (procedural_edit_mode.cpp).
		struct Range
		{
			float OasisParams::* member;
			float min;
			float max;
		};

		static const Range ranges[] = {
			{ &OasisParams::basinDepth, 0.0f, 60.0f },
			{ &OasisParams::rimWidth, 0.5f, 100.0f },
			{ &OasisParams::outerFalloff, 0.5f, 100.0f },
			{ &OasisParams::terraceSharpness, 0.0f, 1.0f },
			{ &OasisParams::floorNoiseAmplitude, 0.0f, 10.0f },
			{ &OasisParams::rimPercentile, 0.0f, 1.0f },
			{ &OasisParams::waterInset, 2.0f, 100.0f },
			{ &OasisParams::waterDepth, 0.0f, 20.0f },
			{ &OasisParams::grassBand, 0.0f, 100.0f },
			{ &OasisParams::dirtBand, 0.0f, 100.0f },
			{ &OasisParams::bandNoiseAmplitude, 0.0f, 20.0f },
			{ &OasisParams::rockSpacing, 1.0f, 100.0f },
			{ &OasisParams::rockOffset, -20.0f, 20.0f },
			{ &OasisParams::rockOffsetJitter, 0.0f, 20.0f },
			{ &OasisParams::rockYawJitterDeg, 0.0f, 180.0f },
			{ &OasisParams::rockScaleMin, 0.1f, 10.0f },
			{ &OasisParams::rockScaleMax, 0.1f, 10.0f },
			{ &OasisParams::rockSink, 0.0f, 30.0f },
			{ &OasisParams::shelfSink, 0.0f, 30.0f },
			{ &OasisParams::clumpSink, 0.0f, 30.0f },
			{ &OasisParams::cliffWeight, 0.0f, 10.0f },
			{ &OasisParams::shelfWeight, 0.0f, 10.0f },
			{ &OasisParams::clumpWeight, 0.0f, 10.0f },
			{ &OasisParams::palmCell, 1.0f, 50.0f },
			{ &OasisParams::palmDensity, 0.0f, 1.0f },
			{ &OasisParams::palmNearWaterBoost, 0.0f, 10.0f },
			{ &OasisParams::palmScaleMin, 0.1f, 10.0f },
			{ &OasisParams::palmScaleMax, 0.1f, 10.0f },
			{ &OasisParams::bushCell, 1.0f, 50.0f },
			{ &OasisParams::bushDensity, 0.0f, 1.0f },
			{ &OasisParams::waterPlantBand, 0.0f, 50.0f },
			{ &OasisParams::waterPlantDensity, 0.0f, 1.0f },
			{ &OasisParams::maxSlopeDeg, 0.0f, 90.0f },
		};

		const OasisParams defaults;
		for (const Range& range : ranges)
		{
			float& value = params.*range.member;
			if (!std::isfinite(value))
			{
				value = defaults.*range.member;
			}
			value = std::min(range.max, std::max(range.min, value));
		}

		params.terraceSteps = std::min<uint32>(params.terraceSteps, 16);

		if (params.rockScaleMin > params.rockScaleMax)
		{
			std::swap(params.rockScaleMin, params.rockScaleMax);
		}
		if (params.palmScaleMin > params.palmScaleMax)
		{
			std::swap(params.palmScaleMin, params.palmScaleMax);
		}
	}

	OasisError PrepareOasis(const PathSpline& loop, uint64 seed, const OasisParams& requestedParams,
		const std::function<float(float, float)>& baseHeight, OasisShape& out)
	{
		OasisParams params = requestedParams;
		SanitizeParams(params);

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
		out.waterLevel = out.floorHeight - kShoreFreeboard;

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

			// The pool is carved into the floor: from its edge the ground drops below the water to the bed.
			if (d < -p.waterInset)
			{
				const float poolBed = shape.waterLevel - p.waterDepth;
				const float u = smoothstep((-d - p.waterInset) / kPoolShoreWidth);
				target.height += (poolBed - shape.floorHeight) * u;
			}
		}
		else
		{
			target.height = shape.rimHeight;
		}

		// Water: a little past the pool edge, the dry shore hides the outline of the quads.
		if (d < 0.0f && d < -p.waterInset + kWaterQuadMargin)
		{
			target.waterType = kWaterTypeWater;
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

namespace mmo::pcg
{
	void BuildPlacements(const OasisShape& shape, const OasisAssets& assets,
		const std::function<float(float, float)>& ground, const std::function<float(float, float)>& slope,
		std::vector<Placement>& out)
	{
		out.clear();
		if (!shape.path)
		{
			return;
		}

		const OasisParams& p = shape.params;
		const SampledPath& path = *shape.path;

		// Rim rocks by arc length steps.
		const uint64 rimSeed = RuleSeed(shape.seed, kRuleRim);
		const int64 stepCount = static_cast<int64>(std::floor(path.GetLength() / p.rockSpacing));
		for (int64 i = 0; i < stepCount; ++i)
		{
			const std::vector<String>* family = PickFamily(HashToUnit(Hash(rimSeed, i, 0, 1)), p, assets);
			if (family == nullptr)
			{
				continue;
			}

			const float distance = static_cast<float>(i) * p.rockSpacing;
			const SplineSample sample = path.SampleAtDistance(distance);
			float nx = 0.0f, nz = 0.0f;
			path.NormalAtDistance(distance, nx, nz);

			const float offset = p.rockOffset + HashRange(Hash(rimSeed, i, 0, 2), -1.0f, 1.0f) * p.rockOffsetJitter;
			const float px = sample.x + nx * offset;
			const float pz = sample.z + nz * offset;
			const float yawJitter = HashRange(Hash(rimSeed, i, 0, 3), -p.rockYawJitterDeg, p.rockYawJitterDeg) * kPi / 180.0f;
			// Faces the basin: local +Z is turned onto the inward direction.
			const float yaw = std::atan2(-nx, -nz) + yawJitter;
			const float scale = HashRange(Hash(rimSeed, i, 0, 4), p.rockScaleMin, p.rockScaleMax);

			Placement placement;
			placement.key = Hash(rimSeed, i, 0, 0);
			placement.kind = PlacementKind::Entity;
			placement.asset = PickAsset(HashToUnit(Hash(rimSeed, i, 0, 5)), *family);
			const float sink = family == &assets.shelves ? p.shelfSink : (family == &assets.clumps ? p.clumpSink : p.rockSink);
			placement.position = Vector3(px, ground(px, pz) - sink, pz);
			placement.rotation = YawRotation(yaw);
			placement.scale = Vector3(scale, scale, scale);
			placement.collides = true;
			out.push_back(std::move(placement));
		}

		// Vegetation by world anchored cells.
		struct VegRule
		{
			uint64 rule;
			float cell;
			const std::vector<String>* assets;
			bool collides;
			float scaleMin, scaleMax;
		};
		const VegRule rules[3] = {
			{ kRulePalm, p.palmCell, &assets.palms, true, p.palmScaleMin, p.palmScaleMax },
			{ kRuleBush, p.bushCell, &assets.bushes, false, 0.8f, 1.2f },
			{ kRulePlant, p.bushCell, &assets.waterPlants, false, 0.8f, 1.2f }
		};

		// Land band: from 1 m outside the pool edge to 1 m inside the rim line.
		const float bandOuter = -1.0f;
		const float bandInner = -(p.waterInset - 1.0f);
		for (const VegRule& rule : rules)
		{
			if (rule.assets->empty() || rule.cell <= 0.0f)
			{
				continue;
			}

			const uint64 ruleSeed = RuleSeed(shape.seed, rule.rule);
			const int64 cx0 = static_cast<int64>(std::floor(shape.influence.minX / rule.cell));
			const int64 cx1 = static_cast<int64>(std::floor(shape.influence.maxX / rule.cell));
			const int64 cz0 = static_cast<int64>(std::floor(shape.influence.minZ / rule.cell));
			const int64 cz1 = static_cast<int64>(std::floor(shape.influence.maxZ / rule.cell));
			for (int64 cz = cz0; cz <= cz1; ++cz)
			{
				for (int64 cx = cx0; cx <= cx1; ++cx)
				{
					const float x = (static_cast<float>(cx) + HashToUnit(Hash(ruleSeed, cx, cz, 1))) * rule.cell;
					const float z = (static_cast<float>(cz) + HashToUnit(Hash(ruleSeed, cx, cz, 2))) * rule.cell;
					const float d = path.SignedDistance(x, z);
					if (d >= 0.0f)
					{
						continue;
					}

					float density;
					if (rule.rule == kRulePlant)
					{
						if (d < -p.waterInset || d > -p.waterInset + p.waterPlantBand)
						{
							continue;
						}
						density = p.waterPlantDensity;
					}
					else
					{
						if (!(d <= bandOuter && d >= bandInner))
						{
							continue;
						}
						density = rule.rule == kRulePalm ? p.palmDensity : p.bushDensity;
						if (rule.rule == kRulePalm)
						{
							// Rises linearly toward the pool.
							const float nearWater = saturate((bandOuter - d) / std::max(0.001f, bandOuter - bandInner));
							density *= 1.0f + p.palmNearWaterBoost * nearWater;
						}
					}

					if (HashToUnit(Hash(ruleSeed, cx, cz, 3)) >= density)
					{
						continue;
					}

					const float y = ground(x, z);
					// Nothing stands under water.
					if (y < shape.waterLevel)
					{
						continue;
					}
					if (slope(x, z) > p.maxSlopeDeg)
					{
						continue;
					}

					const float yaw = HashRange(Hash(ruleSeed, cx, cz, 4), 0.0f, 2.0f * kPi);
					const float scale = HashRange(Hash(ruleSeed, cx, cz, 5), rule.scaleMin, rule.scaleMax);

					Placement placement;
					placement.key = Hash(ruleSeed, cx, cz, 0);
					placement.kind = PlacementKind::Foliage;
					placement.asset = PickAsset(HashToUnit(Hash(ruleSeed, cx, cz, 6)), *rule.assets);
					placement.position = Vector3(x, y, z);
					placement.rotation = YawRotation(yaw);
					placement.scale = Vector3(scale, scale, scale);
					placement.collides = rule.collides;
					out.push_back(std::move(placement));
				}
			}
		}

		std::sort(out.begin(), out.end(), [](const Placement& a, const Placement& b) { return a.key < b.key; });
	}
}
