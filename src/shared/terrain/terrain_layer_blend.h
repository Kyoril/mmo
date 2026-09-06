// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <algorithm>
#include <array>

#include "base/typedefs.h"

namespace mmo::terrain
{
	/// @brief Per-layer inputs to the height based terrain layer blend.
	struct LayerBlendParams
	{
		/// @brief Sampled height for each layer, 0-1.
		std::array<float, 4> height{ { 0.0f, 0.0f, 0.0f, 0.0f } };

		/// @brief Per-layer height scale. 0 disables the height contribution of that layer.
		std::array<float, 4> heightScale{ { 0.0f, 0.0f, 0.0f, 0.0f } };

		/// @brief Per-layer height offset. 1 is the neutral value.
		std::array<float, 4> heightOffset{ { 1.0f, 1.0f, 1.0f, 1.0f } };

		/// @brief Hardness of the transition. 0 reproduces the plain coverage blend exactly,
		///        1 is the fully sharpened transition.
		float sharpness = 0.0f;
	};

	/// @brief Blends four normalized terrain coverage weights using per-layer height maps.
	/// @details This is the reference implementation of the blend the terrain material graph
	///          performs in the pixel shader. It exists so the maths can be tested without a
	///          GPU, and so the node wiring in Models/Terrain/Oakenshire_BoarTerrain.hmat has
	///          an executable specification to match. tools/terrain_blend_check.py verifies the
	///          shipped graph against the same property.
	///
	///          The shape follows the blend WoW introduced in Legion, with one addition. Their
	///          formula is
	///
	///              pct = w * (height * heightScale + heightOffset)
	///              pct = pct * (1 - clamp(max(pct) - pct, 0, 1))
	///              pct = pct / sum(pct)
	///
	///          and the middle step is NOT the identity even when the height term is disabled:
	///          the weights already sum to one, so it reduces to pct = w * (1 - max(w) + w),
	///          which quietly re-contrasts every existing blend. (0.6, 0.4, 0, 0) comes back
	///          as (0.652, 0.348).
	///
	///          Multiplying the clamped term by @c sharpness fixes that. At sharpness 0 every
	///          factor is exactly 1.0, so the result is bit-identical to the plain
	///          coverage-weight normalize the shader did before height blending existed - which
	///          is what lets the shipped terrain material be upgraded in place with no visual
	///          change. At sharpness 1 it reproduces the original formula.
	///          NOTE one deliberate divergence from the graph: this guards the final divide
	///          against a zero sum, the shader does not. Where every layer's contribution is
	///          zero this returns zeros and the shader produces NaN. That is pre-existing -
	///          the plain normalize it replaced divided by the same unguarded sum - so it is
	///          not a regression, but do not read this helper as proof the shader is safe
	///          there. Adding a Max(sum, epsilon) node to the graph would close it for good.
	/// @param weights Normalized coverage weights, expected to sum to 1.
	/// @param params Per-layer height parameters and the transition hardness.
	/// @return The blended weights, summing to 1.
	inline std::array<float, 4> BlendLayerWeights(const std::array<float, 4>& weights, const LayerBlendParams& params)
	{
		std::array<float, 4> pct{};
		for (uint8 i = 0; i < 4; ++i)
		{
			pct[i] = weights[i] * (params.height[i] * params.heightScale[i] + params.heightOffset[i]);
		}

		const float maximum = std::max(std::max(pct[0], pct[1]), std::max(pct[2], pct[3]));

		float sum = 0.0f;
		for (uint8 i = 0; i < 4; ++i)
		{
			pct[i] = pct[i] * (1.0f - std::clamp((maximum - pct[i]) * params.sharpness, 0.0f, 1.0f));
			sum += pct[i];
		}

		if (sum <= 0.0f)
		{
			return pct;
		}

		for (uint8 i = 0; i < 4; ++i)
		{
			pct[i] = pct[i] / sum;
		}

		return pct;
	}
}
