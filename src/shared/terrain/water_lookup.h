// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain/constants.h"

namespace mmo
{
	namespace terrain
	{
		/// @brief Pure page-local water queries.
		///
		/// @remark Deliberately free of any dependency on the terrain library itself so it compiles
		///			into the headless terrain_tests target, which links only base and math.
		///
		/// @remark The central invariant: water presence is carried by the per-tile quad mask, never
		///			by the surface height. Page water heights are zero-initialised on every page, so a
		///			height of 0 means "no water here" exactly as often as it means "the surface is at
		///			y=0". Any caller that keys off height alone concludes that every character
		///			standing below sea level is swimming.
		namespace water_lookup
		{
			/// @brief Number of water sub-quads along one tile side.
			constexpr uint32 QuadsPerTileSide = 8;

			/// @brief Side length of one water sub-quad in world units.
			constexpr float QuadSize = static_cast<float>(constants::TileSize) / static_cast<float>(QuadsPerTileSide);

			/// @brief Non-owning view of one page's water arrays.
			/// @remark Any pointer may be null, which is how a page that never carried a water chunk,
			///			or one that is currently unloaded, presents itself.
			struct PageWaterView
			{
				/// @brief TilesPerPage^2 entries. Bit (qx + qz * QuadsPerTileSide) set means that
				///			sub-quad carries water.
				const uint64* quadMasks{ nullptr };

				/// @brief TilesPerPage^2 entries, each a terrain::WaterType value.
				const uint8* types{ nullptr };

				/// @brief OuterVerticesPerPageSide^2 entries, the water surface height grid.
				const float* vertexHeights{ nullptr };
			};

			/// @brief The address of one water sub-quad within a page, plus whether the source
			///			position actually fell inside the page.
			struct QuadCoord
			{
				uint32 tileX{ 0 };
				uint32 tileZ{ 0 };
				uint32 qx{ 0 };
				uint32 qz{ 0 };
				bool valid{ false };
			};

			/// @brief Checks whether a given sub-quad carries water.
			/// @param view The page's water arrays.
			/// @param localTileX Tile X within the page, [0, TilesPerPage).
			/// @param localTileZ Tile Z within the page, [0, TilesPerPage).
			/// @param qx Sub-quad X within the tile, [0, QuadsPerTileSide).
			/// @param qz Sub-quad Z within the tile, [0, QuadsPerTileSide).
			/// @return True when the quad carries water. Out-of-range indices and null views yield false.
			inline bool HasWaterAtQuad(const PageWaterView& view, const uint32 localTileX, const uint32 localTileZ,
				const uint32 qx, const uint32 qz)
			{
				if (view.quadMasks == nullptr)
				{
					return false;
				}

				if (localTileX >= constants::TilesPerPage || localTileZ >= constants::TilesPerPage)
				{
					return false;
				}

				if (qx >= QuadsPerTileSide || qz >= QuadsPerTileSide)
				{
					return false;
				}

				const uint64 mask = view.quadMasks[localTileX + localTileZ * constants::TilesPerPage];
				return (mask & (1ULL << (qx + qz * QuadsPerTileSide))) != 0ULL;
			}

			/// @brief Gets the liquid type of a tile.
			/// @param view The page's water arrays.
			/// @param localTileX Tile X within the page, [0, TilesPerPage).
			/// @param localTileZ Tile Z within the page, [0, TilesPerPage).
			/// @return The tile's liquid type. Out-of-range indices and null views yield WaterType::None.
			/// @remark This reports the type byte even for a tile whose quads have all been erased.
			///			Callers deciding whether a position is in water must gate on HasWaterAtQuad first.
			inline WaterType TypeAtTile(const PageWaterView& view, const uint32 localTileX, const uint32 localTileZ)
			{
				if (view.types == nullptr)
				{
					return WaterType::None;
				}

				if (localTileX >= constants::TilesPerPage || localTileZ >= constants::TilesPerPage)
				{
					return WaterType::None;
				}

				return static_cast<WaterType>(view.types[localTileX + localTileZ * constants::TilesPerPage]);
			}

			/// @brief Converts a page-local position in world units to the sub-quad containing it.
			/// @param localX Position along X relative to the page origin, in world units.
			/// @param localZ Position along Z relative to the page origin, in world units.
			/// @return The quad address. `valid` is false when the position lies outside the page.
			inline QuadCoord QuadFromPageLocal(const float localX, const float localZ)
			{
				QuadCoord result;

				if (localX < 0.0f || localZ < 0.0f)
				{
					return result;
				}

				const float quadX = localX / QuadSize;
				const float quadZ = localZ / QuadSize;

				constexpr uint32 quadsPerPageSide = constants::TilesPerPage * QuadsPerTileSide;
				if (quadX >= static_cast<float>(quadsPerPageSide) || quadZ >= static_cast<float>(quadsPerPageSide))
				{
					return result;
				}

				const uint32 gx = static_cast<uint32>(quadX);
				const uint32 gz = static_cast<uint32>(quadZ);

				result.tileX = gx / QuadsPerTileSide;
				result.tileZ = gz / QuadsPerTileSide;
				result.qx = gx % QuadsPerTileSide;
				result.qz = gz % QuadsPerTileSide;
				result.valid = true;
				return result;
			}
		}
	}
}
