// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/math_utils.h"
#include "terrain/constants.h"

#include <vector>

namespace mmo
{
	namespace terrain_io
	{
		/// @brief Owning, render-independent representation of a single terrain page as
		///	       stored in a v2 .tile file. Used by headless tools and tests.
		struct PageData
		{
			/// @brief Outer grid heightmap (OuterVerticesPerPageSide² floats).
			std::vector<float> heightmap;

			/// @brief Inner grid heightmap (InnerVerticesPerPageSide² floats).
			std::vector<float> innerHeightmap;

			/// @brief Encoded outer vertex normals (OuterVerticesPerPageSide² entries).
			std::vector<EncodedNormal8> normals;

			/// @brief Encoded inner vertex normals (InnerVerticesPerPageSide² entries).
			std::vector<EncodedNormal8> innerNormals;

			/// @brief Per-tile material asset names (TilesPerPage² entries). An empty string
			///	       means the tile uses the world's default terrain material.
			std::vector<String> materialNames;

			/// @brief Packed splat layers (PixelsPerPage² entries, 4 layer weights per uint32,
			///	       layer 0 in the lowest byte).
			std::vector<uint32> layers;

			/// @brief Outer vertex colors in ARGB (OuterVerticesPerPageSide² entries).
			std::vector<uint32> colors;

			/// @brief Inner vertex colors in ARGB (InnerVerticesPerPageSide² entries).
			std::vector<uint32> innerColors;

			/// @brief Per-tile hole bit masks (TilesPerPage² entries, one bit per inner vertex).
			std::vector<uint64> holes;

			/// @brief Per-tile zone / area ids (TilesPerPage² entries).
			std::vector<uint32> zones;

			/// @brief Per-tile water quad bit masks (TilesPerPage² entries, 0 = no water).
			std::vector<uint64> waterQuadMasks;

			/// @brief Per-tile water type (TilesPerPage² entries, see terrain::WaterType).
			std::vector<uint8> waterTypes;

			/// @brief Shared page-level water vertex heights (OuterVerticesPerPageSide² floats).
			std::vector<float> waterVertexHeights;

			/// @brief Optional water material asset name.
			String waterMaterialName;

			/// @brief Resizes all arrays to their expected sizes and fills them with the same
			///	       defaults a freshly prepared, empty page would have.
			void Reset();
		};

		/// @brief Non-owning view over the data of a single terrain page, used as the
		///	       serialization input. Pointers reference arrays of the fixed sizes
		///	       documented on PageData; optional chunks may be null.
		struct PageDataView
		{
			/// @brief Outer heightmap, OuterVerticesPerPageSide² floats. Required.
			const float *heightmap = nullptr;

			/// @brief Inner heightmap, InnerVerticesPerPageSide² floats. Required.
			const float *innerHeightmap = nullptr;

			/// @brief Encoded outer normals, OuterVerticesPerPageSide² entries. Required.
			const EncodedNormal8 *normals = nullptr;

			/// @brief Encoded inner normals, InnerVerticesPerPageSide² entries. Required.
			const EncodedNormal8 *innerNormals = nullptr;

			/// @brief Per-tile material names, materialCount entries. May be null if materialCount is 0.
			const String *materialNames = nullptr;

			/// @brief Number of entries in materialNames (TilesPerPage² for pages written by the editor).
			size_t materialCount = 0;

			/// @brief Packed splat layers, PixelsPerPage² entries. Required.
			const uint32 *layers = nullptr;

			/// @brief Outer vertex colors, OuterVerticesPerPageSide² entries. Required.
			const uint32 *colors = nullptr;

			/// @brief Inner vertex colors, InnerVerticesPerPageSide² entries. Required.
			const uint32 *innerColors = nullptr;

			/// @brief Per-tile hole masks, TilesPerPage² entries. May be null (no holes chunk).
			const uint64 *holes = nullptr;

			/// @brief Per-tile zone ids, TilesPerPage² entries. Required.
			const uint32 *zones = nullptr;

			/// @brief Per-tile water quad masks, TilesPerPage² entries. May be null (no water chunk).
			const uint64 *waterQuadMasks = nullptr;

			/// @brief Per-tile water types, TilesPerPage² entries. Required if waterQuadMasks is set.
			const uint8 *waterTypes = nullptr;

			/// @brief Water vertex heights, OuterVerticesPerPageSide² floats. Required if waterQuadMasks is set.
			const float *waterVertexHeights = nullptr;

			/// @brief Optional water material name. May be null.
			const String *waterMaterialName = nullptr;
		};

		/// @brief Builds a full view over an owning PageData instance.
		///	@param data The page data to reference. Must outlive the returned view.
		///	@return A view referencing all arrays of the given page data.
		PageDataView MakeView(const PageData &data);
	}
}
