// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "constants.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace terrain
	{
		/// Pure, GPU-free index and interpolation math for terrain region operations
		/// (capture/apply/fill/stamp). Everything here is unit-testable in isolation.
		namespace region_math
		{
			/// Vertex cells per page side on the outer-vertex grid (128).
			constexpr int32 CellsPerPage = static_cast<int32>(constants::OuterVerticesPerPageSide) - 1;

			/// Pixel cells per page side on the coverage grid (1008).
			constexpr int32 PixelCellsPerPage = static_cast<int32>(constants::PixelsPerPage) - 1;

			/// Exact rational ratio between pixel cells and vertex cells: 1008/128 = 63/8.
			constexpr int32 PixelRatioNum = 63;
			constexpr int32 PixelRatioDen = 8;
			static_assert(CellsPerPage * PixelRatioNum == PixelCellsPerPage * PixelRatioDen,
				"Pixel/vertex grid ratio must be exactly 63/8");

			/// A rectangle on the global outer-vertex grid. minX/minZ are the minimum outer-vertex
			/// indices; sizeX/sizeZ count *cells*, so the rect covers outer vertices
			/// [minX .. minX+sizeX] inclusive and exactly sizeX*sizeZ inner (cell-center) vertices.
			struct VertexRect
			{
				int32 minX = 0;
				int32 minZ = 0;
				int32 sizeX = 0;
				int32 sizeZ = 0;

				/// True if the rect covers no cells.
				[[nodiscard]] bool IsEmpty() const
				{
					return sizeX <= 0 || sizeZ <= 0;
				}
			};

			/// Floor division that is correct for negative numerators.
			inline int32 floorDiv(const int32 a, const int32 b)
			{
				int32 q = a / b;
				if ((a % b != 0) && ((a < 0) != (b < 0)))
				{
					--q;
				}
				return q;
			}

			/// Ceiling division that is correct for negative numerators.
			inline int32 ceilDiv(const int32 a, const int32 b)
			{
				return -floorDiv(-a, b);
			}

			/// Clamps a rect to the terrain's cell bounds, shrinking it as needed.
			inline VertexRect ClampToBounds(const VertexRect& rect, const int32 widthPages, const int32 heightPages)
			{
				const int32 maxCellsX = widthPages * CellsPerPage;
				const int32 maxCellsZ = heightPages * CellsPerPage;

				const int32 x0 = std::clamp(rect.minX, 0, maxCellsX);
				const int32 z0 = std::clamp(rect.minZ, 0, maxCellsZ);
				const int32 x1 = std::clamp(rect.minX + rect.sizeX, 0, maxCellsX);
				const int32 z1 = std::clamp(rect.minZ + rect.sizeZ, 0, maxCellsZ);

				return VertexRect{ x0, z0, x1 - x0, z1 - z0 };
			}

			/// World position of a global outer vertex index (terrain is centered on the origin).
			inline float VertexToWorld(const int32 v, const int32 pages)
			{
				const double scale = constants::PageSize / static_cast<double>(CellsPerPage);
				const double half = pages * constants::PageSize * 0.5;
				return static_cast<float>(v * scale - half);
			}

			/// Nearest global outer vertex index for a world coordinate, clamped to bounds.
			inline int32 RoundWorldToVertex(const float world, const int32 pages)
			{
				const double scale = constants::PageSize / static_cast<double>(CellsPerPage);
				const double half = pages * constants::PageSize * 0.5;
				const int32 v = static_cast<int32>(std::lround((world + half) / scale));
				return std::clamp(v, 0, pages * CellsPerPage);
			}

			/// Smallest clamped vertex rect whose world footprint contains the circle
			/// (centerX, centerZ, radius).
			inline VertexRect VertexRectForBrush(const float centerX, const float centerZ, const float radius,
				const int32 widthPages, const int32 heightPages)
			{
				const double scale = constants::PageSize / static_cast<double>(CellsPerPage);
				const double halfW = widthPages * constants::PageSize * 0.5;
				const double halfH = heightPages * constants::PageSize * 0.5;

				const int32 x0 = static_cast<int32>(std::floor((centerX - radius + halfW) / scale));
				const int32 x1 = static_cast<int32>(std::ceil((centerX + radius + halfW) / scale));
				const int32 z0 = static_cast<int32>(std::floor((centerZ - radius + halfH) / scale));
				const int32 z1 = static_cast<int32>(std::ceil((centerZ + radius + halfH) / scale));

				return ClampToBounds(VertexRect{ x0, z0, x1 - x0, z1 - z0 }, widthPages, heightPages);
			}

			/// Inclusive pixel index range whose world positions lie inside the vertex rect.
			inline void PixelRangeInside(const VertexRect& rect, int32& minPX, int32& minPZ, int32& maxPX, int32& maxPZ)
			{
				minPX = ceilDiv(rect.minX * PixelRatioNum, PixelRatioDen);
				minPZ = ceilDiv(rect.minZ * PixelRatioNum, PixelRatioDen);
				maxPX = floorDiv((rect.minX + rect.sizeX) * PixelRatioNum, PixelRatioDen);
				maxPZ = floorDiv((rect.minZ + rect.sizeZ) * PixelRatioNum, PixelRatioDen);
			}

			/// Inclusive pixel index range that fully covers the vertex rect's world span,
			/// padded outward by up to one pixel for resampling.
			inline void PixelRangePadded(const VertexRect& rect, int32& minPX, int32& minPZ, int32& maxPX, int32& maxPZ)
			{
				minPX = floorDiv(rect.minX * PixelRatioNum, PixelRatioDen);
				minPZ = floorDiv(rect.minZ * PixelRatioNum, PixelRatioDen);
				maxPX = ceilDiv((rect.minX + rect.sizeX) * PixelRatioNum, PixelRatioDen);
				maxPZ = ceilDiv((rect.minZ + rect.sizeZ) * PixelRatioNum, PixelRatioDen);
			}

			/// Maps a destination pixel index to the nearest source pixel index for a paste whose
			/// source rect starts at srcMinVert and destination rect at destMinVert (same axis).
			inline int32 MapDestPixelToSourcePixel(const int32 destPixel, const int32 srcMinVert, const int32 destMinVert)
			{
				const double shift = (srcMinVert - destMinVert) * (static_cast<double>(PixelRatioNum) / PixelRatioDen);
				return static_cast<int32>(std::lround(destPixel + shift));
			}

			/// Inclusive range of tiles (8-cell squares) fully covered by the rect.
			/// Returns false if no tile is fully covered.
			inline bool TileRangeFullyCovered(const VertexRect& rect, int32& minTX, int32& minTZ, int32& maxTX, int32& maxTZ)
			{
				constexpr int32 cellsPerTile = static_cast<int32>(constants::OuterVerticesPerTileSide) - 1;
				minTX = ceilDiv(rect.minX, cellsPerTile);
				minTZ = ceilDiv(rect.minZ, cellsPerTile);
				maxTX = floorDiv(rect.minX + rect.sizeX, cellsPerTile) - 1;
				maxTZ = floorDiv(rect.minZ + rect.sizeZ, cellsPerTile) - 1;
				return maxTX >= minTX && maxTZ >= minTZ;
			}

			/// Maps a destination tile index to the source tile whose center corresponds to it
			/// after shifting by (srcMinVert - destMinVert) cells.
			inline int32 MapDestTileToSourceTile(const int32 destTile, const int32 srcMinVert, const int32 destMinVert)
			{
				constexpr int32 cellsPerTile = static_cast<int32>(constants::OuterVerticesPerTileSide) - 1;
				const int32 centerCell = destTile * cellsPerTile + cellsPerTile / 2 + (srcMinVert - destMinVert);
				return floorDiv(centerCell, cellsPerTile);
			}

			/// Bilinearly blended Coons patch: interpolates an interior height from the four border
			/// curves so that every border value is reproduced exactly.
			/// left/right are the border heights at parameter v on the x=min / x=max edges;
			/// top/bottom at parameter u on the z=min / z=max edges; hXY are the corners
			/// (h00 = min/min, h10 = max/min x/z ... h11 = max/max).
			inline float CoonsHeight(const float u, const float v,
				const float left, const float right, const float top, const float bottom,
				const float h00, const float h10, const float h01, const float h11)
			{
				return (1.0f - u) * left + u * right
					+ (1.0f - v) * top + v * bottom
					- ((1.0f - u) * (1.0f - v) * h00 + u * (1.0f - v) * h10
						+ (1.0f - u) * v * h01 + u * v * h11);
			}
		}
	}
}
