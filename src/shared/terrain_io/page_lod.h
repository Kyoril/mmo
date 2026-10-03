// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/math_utils.h"
#include "terrain/constants.h"

#include <vector>

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	namespace terrain_io
	{
		struct PageData;

		/// @brief Distance between two LOD vertices, in outer page vertices.
		constexpr uint32 LodVertexStride = 4;

		/// @brief Number of LOD vertices along one page side (every fourth outer vertex, both edges included).
		constexpr uint32 LodVerticesPerPageSide = (terrain::constants::OuterVerticesPerPageSide - 1) / LodVertexStride + 1;

		/// @brief Number of LOD vertices of one page.
		constexpr uint32 LodVertexCount = LodVerticesPerPageSide * LodVerticesPerPageSide;

		static_assert((LodVerticesPerPageSide - 1) * LodVertexStride == terrain::constants::OuterVerticesPerPageSide - 1,
			"The LOD grid must land exactly on the page's far edge so neighbouring LOD pages share their border");

		/// @brief Low-resolution stand-in of one terrain page, rendered as distant terrain (.tlod file).
		/// @details Indices are `i + j * LodVerticesPerPageSide`, with i along +X and j along +Z, sampled
		///	         at outer vertex (i * LodVertexStride, j * LodVertexStride) of the page.
		struct PageLodData
		{
			/// @brief Page-local heights, LodVertexCount entries.
			std::vector<float> heights;

			/// @brief Encoded vertex normals, LodVertexCount entries.
			std::vector<EncodedNormal8> normals;

			/// @brief Resizes both arrays to LodVertexCount: height 0, normal straight up.
			void Reset();
		};

		/// @brief The page data BuildPageLod reads. Optional water arrays may be null.
		struct PageLodSource
		{
			/// @brief Outer heightmap, OuterVerticesPerPageSide² floats. Required.
			const float *heightmap = nullptr;

			/// @brief Encoded outer normals, OuterVerticesPerPageSide² entries. Required.
			const EncodedNormal8 *normals = nullptr;

			/// @brief Per-tile water quad masks, TilesPerPage² entries. May be null (no water).
			const uint64 *waterQuadMasks = nullptr;

			/// @brief Water surface heights, OuterVerticesPerPageSide² floats. Required if waterQuadMasks is set.
			const float *waterVertexHeights = nullptr;
		};

		/// @brief Builds a LOD source referencing an owning PageData instance.
		PageLodSource MakeLodSource(const PageData &data);

		/// @brief Derives a page's LOD from its full-resolution data.
		/// @details Heights are point-sampled at the LOD grid so the border vertices coincide with the
		///	         full-resolution page and with neighbouring LOD pages. A sample touching a water quad is
		///	         raised to the water surface (never lowered), so distant seas show their surface rather
		///	         than their floor, and gets an upward normal. All other normals are the normalised mean
		///	         of the full-resolution normals within half a LOD cell, which keeps the lighting of
		///	         rough terrain from aliasing.
		void BuildPageLod(const PageLodSource &source, PageLodData &out);

		/// @brief Serializes page LOD data in the .tlod chunk format (MVER, LHGT, LNRM).
		bool SavePageLod(io::Writer &writer, const PageLodData &lod);

		/// @brief Deserializes .tlod page LOD data.
		/// @return false if the data is malformed, of an unsupported version or lacks a required chunk.
		bool LoadPageLod(io::Reader &reader, PageLodData &out);
	}
}
