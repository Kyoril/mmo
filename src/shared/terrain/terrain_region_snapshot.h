// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain_region_math.h"

#include <vector>

namespace mmo
{
	namespace terrain
	{
		/// A full capture of every terrain channel inside a vertex-aligned rectangle.
		/// Used as the clipboard for region copy/cut/paste, as the cut-fill source, and as
		/// the undo/redo payload. Produced by Terrain::CaptureRegion, consumed by
		/// Terrain::ApplyRegion.
		struct TerrainRegionSnapshot
		{
			/// The captured outer-vertex cell rect (see region_math::VertexRect semantics).
			region_math::VertexRect rect;

			/// Outer-vertex heights, (sizeX+1) * (sizeZ+1), row-major by z.
			std::vector<float> outerHeights;

			/// Outer-vertex colors (ARGB), same layout as outerHeights.
			std::vector<uint32> outerColors;

			/// Inner (cell-center) vertex heights, sizeX * sizeZ, row-major by z.
			std::vector<float> innerHeights;

			/// Inner vertex colors (ARGB), same layout as innerHeights.
			std::vector<uint32> innerColors;

			/// Hole flags per inner vertex (1 = hole), same layout as innerHeights.
			std::vector<uint8> holes;

			/// Padded splat pixel capture range (inclusive origin + counts).
			int32 minPixelX = 0;
			int32 minPixelZ = 0;
			int32 pixelCountX = 0;
			int32 pixelCountZ = 0;

			/// Packed 4-layer coverage per captured pixel, row-major by z.
			std::vector<uint32> splatPixels;

			/// Tiles fully covered by the rect (inclusive origin + counts; counts may be 0).
			int32 minTileX = 0;
			int32 minTileZ = 0;
			int32 tileCountX = 0;
			int32 tileCountZ = 0;

			/// Area IDs of the fully covered tiles, row-major by z.
			std::vector<uint32> areaIds;

			/// True if the snapshot covers at least one cell.
			[[nodiscard]] bool IsValid() const
			{
				return !rect.IsEmpty();
			}
		};
	}
}
