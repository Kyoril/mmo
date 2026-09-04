// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Names the shader parameters that drive one terrain splatting layer.
	/// @details Terrain materials are hand-authored node graphs, and every one of them names
	///          its per-layer parameters differently: the shipped terrain material uses
	///          "scale_textures_Grass" and "layer2_textures_scaling", while Models/Grass.hmat
	///          uses "Tiling01". Without a declared binding no tool can find those parameters,
	///          which is why terrain layer properties were only ever reachable by opening the
	///          material instance in the material editor.
	///
	///          A binding is a pure lookup table: it stores parameter NAMES, never values. The
	///          values continue to live in the material's scalar and texture parameter lists,
	///          so a binding cannot desync from what the compiled graph actually samples.
	///
	///          An empty field means "this material does not expose that control for this
	///          layer" and editors are expected to disable the corresponding widget. A material
	///          with an entirely empty binding table behaves exactly as it did before this
	///          existed.
	struct MaterialLayerBinding
	{
		/// @brief Human readable layer name shown in the terrain paint panel, e.g. "Grass".
		///        Falls back to "Layer N" when empty.
		String displayName;

		/// @brief Name of the texture parameter holding this layer's base color.
		String albedoTextureParam;

		/// @brief Name of the texture parameter holding this layer's normal map.
		String normalTextureParam;

		/// @brief Name of the scalar parameter scaling this layer's UVs.
		/// @details This is a DIVISOR applied to the world position, so its value is
		///          "world units per texture repeat" and a larger number means a larger
		///          texture footprint. Note that the shipped material stores these negative;
		///          editors must preserve the sign or the UVs flip.
		String scaleScalarParam;

		/// @brief Name of the texture parameter holding this layer's height map.
		String heightTextureParam;

		/// @brief Name of the scalar parameter scaling this layer's height contribution.
		///        0 disables height blending for the layer.
		String heightScaleParam;

		/// @brief Name of the scalar parameter offsetting this layer's height contribution.
		///        1 is the neutral value.
		String heightOffsetParam;

		/// @brief Whether any field of this binding is set.
		[[nodiscard]] bool IsEmpty() const
		{
			return displayName.empty()
				&& albedoTextureParam.empty()
				&& normalTextureParam.empty()
				&& scaleScalarParam.empty()
				&& heightTextureParam.empty()
				&& heightScaleParam.empty()
				&& heightOffsetParam.empty();
		}
	};

	/// @brief Number of terrain splatting layers a material can describe. Matches the four
	///        coverage channels packed per pixel by the terrain page format.
	constexpr uint8 MaterialLayerBindingCount = 4;
}
