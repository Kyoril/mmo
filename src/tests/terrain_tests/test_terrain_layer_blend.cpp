// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "base/typedefs.h"
#include "terrain/terrain_layer_blend.h"

#include <random>

using namespace mmo;
using namespace mmo::terrain;

// These tests are the executable specification for the blend built into
// Models/Terrain/Oakenshire_BoarTerrain.hmat. The graph's node wiring has to reproduce them,
// and in particular the first two are the reason that material could be rewired to route its
// coverage weights through the blend without changing a single pixel.
// tools/terrain_blend_check.py checks the shipped graph against the same property.

namespace
{
	// The blend the terrain shader performed before height blending existed: normalize the raw
	// coverage weights against their own sum. The accumulation order matters - it has to match
	// the left-nested ((r+g)+b)+a chain in both the old graph and BlendLayerWeights, or the
	// comparison below is only approximately true instead of exactly true.
	std::array<float, 4> PlainNormalize(const std::array<float, 4>& raw)
	{
		float sum = 0.0f;
		for (const float v : raw)
		{
			sum += v;
		}

		if (sum <= 0.0f)
		{
			return raw;
		}

		std::array<float, 4> result{};
		for (uint8 i = 0; i < 4; ++i)
		{
			result[i] = raw[i] / sum;
		}
		return result;
	}

	// Defaults: no height contribution, neutral offset, no sharpening.
	LayerBlendParams Neutral()
	{
		return LayerBlendParams{};
	}
}

TEST_CASE("Neutral blend parameters reproduce the plain coverage normalize exactly", "[terrain_layer_blend]")
{
	// Exact equality, not Approx. At these defaults every factor in the chain is exactly 1.0,
	// so feeding raw coverage weights through the height blend must produce bit-for-bit what
	// the old splat/sum normalize produced.
	//
	// Note the comparison is against the OLD PIPELINE, not against the input. The shader's
	// input is the raw coverage texture and the divide by the sum is part of both paths;
	// pre-normalizing in float would leave a sum of 1.0 give or take an ulp, and the divide
	// would then legitimately shift the result.
	const std::array<std::array<float, 4>, 7> cases{ {
		{ { 0.6f, 0.4f, 0.0f, 0.0f } },
		{ { 0.75f, 0.25f, 0.0f, 0.0f } },
		{ { 0.25f, 0.25f, 0.25f, 0.25f } },
		{ { 1.0f, 0.0f, 0.0f, 0.0f } },
		{ { 0.5f, 0.2f, 0.2f, 0.1f } },
		{ { 0.0f, 0.0f, 0.0f, 1.0f } },
		{ { 0.37f, 0.11f, 0.29f, 0.83f } },
	} };

	for (const auto& raw : cases)
	{
		const auto blended = BlendLayerWeights(raw, Neutral());
		const auto expected = PlainNormalize(raw);

		REQUIRE(blended[0] == expected[0]);
		REQUIRE(blended[1] == expected[1]);
		REQUIRE(blended[2] == expected[2]);
		REQUIRE(blended[3] == expected[3]);
	}
}

TEST_CASE("Neutral blend is exact for arbitrary raw coverage weights", "[terrain_layer_blend]")
{
	std::mt19937 rng{ 1337 };
	std::uniform_real_distribution<float> dist{ 0.0f, 1.0f };

	for (int i = 0; i < 5000; ++i)
	{
		const std::array<float, 4> raw{ { dist(rng), dist(rng), dist(rng), dist(rng) } };
		if (raw[0] + raw[1] + raw[2] + raw[3] <= 0.0f)
		{
			continue;
		}

		const auto blended = BlendLayerWeights(raw, Neutral());
		const auto expected = PlainNormalize(raw);

		REQUIRE(blended[0] == expected[0]);
		REQUIRE(blended[1] == expected[1]);
		REQUIRE(blended[2] == expected[2]);
		REQUIRE(blended[3] == expected[3]);
	}
}

TEST_CASE("Full sharpness reproduces the original Legion contrast", "[terrain_layer_blend]")
{
	// The hand-computed counterexample from the design: pct = (0.6, 0.4 * 0.8) = (0.6, 0.32),
	// sum 0.92, normalized (0.652, 0.348). This pins that the sharpen step is genuinely not
	// the identity, and that we implemented it the way WoW does.
	LayerBlendParams params = Neutral();
	params.sharpness = 1.0f;

	const auto blended = BlendLayerWeights({ { 0.6f, 0.4f, 0.0f, 0.0f } }, params);

	REQUIRE(blended[0] == Approx(0.6f / 0.92f).epsilon(0.0001f));
	REQUIRE(blended[1] == Approx(0.32f / 0.92f).epsilon(0.0001f));
	REQUIRE(blended[2] == Approx(0.0f));
	REQUIRE(blended[3] == Approx(0.0f));
}

TEST_CASE("A taller layer wins ground from an equally covered neighbour", "[terrain_layer_blend]")
{
	// The point of the whole feature: where coverage is a 50/50 tie, the layer whose height map
	// is higher at that texel should take the pixel outright rather than cross-fading.
	LayerBlendParams params;
	params.height = { { 0.0f, 1.0f, 0.0f, 0.0f } };
	params.heightScale = { { 1.0f, 1.0f, 0.0f, 0.0f } };
	params.sharpness = 1.0f;

	const auto blended = BlendLayerWeights({ { 0.5f, 0.5f, 0.0f, 0.0f } }, params);

	REQUIRE(blended[1] > blended[0]);
	REQUIRE(blended[1] > 0.6f);
}

TEST_CASE("Blend output always sums to one and stays non-negative", "[terrain_layer_blend]")
{
	std::mt19937 rng{ 4242 };
	std::uniform_real_distribution<float> dist{ 0.0f, 1.0f };

	for (int i = 0; i < 5000; ++i)
	{
		const std::array<float, 4> weights{ { dist(rng), dist(rng), dist(rng), dist(rng) } };
		if (weights[0] + weights[1] + weights[2] + weights[3] <= 0.0f)
		{
			continue;
		}

		LayerBlendParams params;
		params.sharpness = dist(rng);
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			params.height[layer] = dist(rng);
			params.heightScale[layer] = dist(rng);
			params.heightOffset[layer] = dist(rng) * 2.0f;
		}

		const auto blended = BlendLayerWeights(weights, params);

		float sum = 0.0f;
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			REQUIRE(blended[layer] >= 0.0f);
			sum += blended[layer];
		}

		REQUIRE(sum == Approx(1.0f).epsilon(0.0001f));
	}
}

TEST_CASE("The CPU helper guards its divide where the shader does not", "[terrain_layer_blend]")
{
	// Reachable when every layer's height offset is zero, e.g. a half-authored material.
	// This pins the HELPER's behaviour only: the graph divides unguarded and produces NaN
	// here. That is pre-existing rather than a regression - the plain normalize this replaced
	// divided by the same unguarded sum - but this test must not be read as evidence that the
	// shader is safe in that case. See the note on BlendLayerWeights.
	LayerBlendParams params;
	params.heightOffset = { { 0.0f, 0.0f, 0.0f, 0.0f } };

	const auto blended = BlendLayerWeights({ { 0.5f, 0.5f, 0.0f, 0.0f } }, params);

	for (uint8 layer = 0; layer < 4; ++layer)
	{
		REQUIRE(blended[layer] == 0.0f);
	}
}
