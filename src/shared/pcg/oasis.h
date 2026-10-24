// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "placement.h"
#include "terrain_contribution.h"
#include "math/path_spline.h"

#include <functional>
#include <memory>
#include <vector>

namespace mmo::pcg
{
	/// Tunable values of the oasis rule set.
	struct OasisParams
	{
		/// Floor below rim height.
		float basinDepth = 6.0f;
		/// Rim to floor blend distance inside the loop.
		float rimWidth = 14.0f;
		/// Blend back to base outside the loop.
		float outerFalloff = 10.0f;
		/// Number of terrace steps; 0 = plain smoothstep.
		uint32 terraceSteps = 2;
		/// 0 soft .. 1 hard steps.
		float terraceSharpness = 0.6f;
		float floorNoiseAmplitude = 0.4f;
		/// Percentile of loop base heights used as rim height.
		float rimPercentile = 0.2f;
		/// Pool edge distance inside the loop. From the edge inward the ground drops into the pool.
		float waterInset = 10.0f;
		/// Pool bed below the water surface. The surface lies just below the basin floor.
		float waterDepth = 1.2f;
		/// Grass (3) to desert grass (2) band inside the rim.
		float grassBand = 6.0f;
		/// Desert grass to dirt (1) band, then sand (0).
		float dirtBand = 4.0f;
		float bandNoiseAmplitude = 2.0f;

		float rockSpacing = 7.0f;
		float rockOffset = 2.0f;
		float rockOffsetJitter = 1.5f;
		float rockYawJitterDeg = 20.0f;
		float rockScaleMin = 0.8f;
		float rockScaleMax = 1.2f;
		/// Cliff pieces are lowered this far into the ground.
		float rockSink = 0.5f;
		float cliffWeight = 0.5f;
		float shelfWeight = 0.25f;
		float clumpWeight = 0.25f;

		float palmCell = 6.0f;
		float palmDensity = 0.45f;
		float palmNearWaterBoost = 1.0f;
		float palmScaleMin = 0.8f;
		float palmScaleMax = 1.3f;
		float bushCell = 3.0f;
		float bushDensity = 0.35f;
		/// Water plants grow on land within this distance of the pool edge.
		float waterPlantBand = 3.0f;
		/// Acceptance probability of a water plant cell.
		float waterPlantDensity = 0.7f;
		float maxSlopeDeg = 30.0f;

		/// Shelf pieces are lowered this far into the ground (the families differ a lot in size).
		float shelfSink = 0.5f;
		/// Clumps are lowered this far into the ground.
		float clumpSink = 0.5f;
	};

	/// Clamps every parameter to the range the Procedural mode UI offers, replaces non finite values
	/// by the defaults, orders inverted min/max pairs (rock and palm scale) and keeps the spacings,
	/// widths and cells above a sane minimum, so no parameter set (hand edited recipe, corrupt
	/// record) can stall or crash the generator. Idempotent.
	void SanitizeParams(OasisParams& params);

	/// Water type of pool quads. Mirrors terrain::WaterType::Water; pcg does not depend on the terrain
	/// library, the editor adapter stores the value as is.
	constexpr uint8 kWaterTypeWater = 1;

	/// Asset names the oasis rules pick from.
	struct OasisAssets
	{
		/// Rim, Entity.
		std::vector<String> cliffs, shelves, clumps;
		/// Foliage, collides.
		std::vector<String> palms;
		/// Foliage, no collision.
		std::vector<String> bushes, waterPlants;
		/// Used by the editor adapter.
		String terrainMaterial, waterMaterial;
	};

	/// Prepared oasis: sampled loop and derived heights. Immutable after PrepareOasis.
	struct OasisShape
	{
		OasisParams params;
		uint64 seed = 0;
		std::shared_ptr<const SampledPath> path;
		float rimHeight = 0.0f;
		/// Height of the dry basin floor around the pool.
		float floorHeight = 0.0f;
		/// Water surface, slightly below the floor.
		float waterLevel = 0.0f;
		WorldRect influence;
	};

	enum class OasisError : uint8
	{
		None,
		TooFewPoints,
		ZeroArea,
		SelfIntersecting
	};

	/// Validates the loop and samples base heights along it (1 m steps). The loop is treated as closed.
	OasisError PrepareOasis(const PathSpline& loop, uint64 seed, const OasisParams& params,
		const std::function<float(float, float)>& baseHeight, OasisShape& out);

	/// Loop bounds + outerFalloff + 2 m.
	WorldRect GetInfluenceRect(const OasisShape& shape);

	/// Water surface height.
	float GetWaterLevel(const OasisShape& shape);

	/// Terrain wish at a position. Splat, water and material results do not depend on baseHeight.
	TerrainTarget EvaluateTerrain(const OasisShape& shape, float x, float z, float baseHeight);

	/// Rim rocks and vegetation. `ground` is the final ground, `slope` returns degrees. Output is
	/// sorted by key.
	void BuildPlacements(const OasisShape& shape, const OasisAssets& assets,
		const std::function<float(float, float)>& ground, const std::function<float(float, float)>& slope,
		std::vector<Placement>& out);
}
