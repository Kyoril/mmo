// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain/constants.h"
#include "terrain/water_lookup.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		/// @brief Builds the camera-centred ocean mask the GPU ocean renderer samples.
		///
		/// The mask is a square window of texels, one per water quad (water_lookup::QuadSize world
		/// units), snapped to the global quad grid so every texel covers exactly one quad. Each texel
		/// has two 8-bit channels:
		///  - R: 255 where the ocean is drawn, 0 where it is not.
		///  - G: water depth below the sea level, (seaLevel - terrainHeight) / DepthEncodeRange,
		///       clamped to [0, 1] and scaled to 255. 0 wherever R is 0.
		///
		/// Texels are stored row by row along +Z, texels within a row along +X:
		/// `out[(tz * texelsPerSide + tx) * ChannelsPerTexel + channel]`. Texel (tx, tz) spans
		/// [originX + tx * TexelSize, originX + (tx + 1) * TexelSize) along X and likewise along Z.
		///
		/// @remark Dependency-free (base + header-only terrain constants) so terrain_tests, which links
		///			only base and math, covers it. Terrain::BuildOceanMask feeds it the live pages.
		namespace ocean_mask
		{
			/// @brief Bytes per texel (R and G).
			constexpr uint32 ChannelsPerTexel = 2;

			/// @brief Water depth, in world units, that encodes as G = 255. The renderer decodes
			///			depth = G / 255 * DepthEncodeRange.
			constexpr float DepthEncodeRange = 32.0f;

			/// @brief Water quads along one page side.
			constexpr uint32 QuadsPerPageSide = constants::TilesPerPage * water_lookup::QuadsPerTileSide;

			/// @brief World size of one texel (one water quad), in double precision so window
			///			origins snap exactly onto the quad grid.
			constexpr double TexelSize = constants::PageSize / static_cast<double>(QuadsPerPageSide);

			/// @brief R value of a texel on which the ocean is drawn.
			constexpr uint8 OceanDrawn = 255;

			/// @brief What the terrain knows about the page under a texel.
			enum class PageState : uint8
			{
				/// @brief Off the map, or not prepared/streamed yet: treated as open ocean (the depth
				///			test against the distant terrain hides it under land).
				Unavailable,

				/// @brief A page without a page file: open ocean everywhere.
				OceanPlaceholder,

				/// @brief A prepared page with real terrain and water data.
				Prepared
			};

			/// @brief Non-owning view of one page for the mask builder.
			struct PageSource
			{
				/// @brief The page's state.
				PageState state{ PageState::Unavailable };

				/// @brief The page's water arrays. Only read for PageState::Prepared.
				water_lookup::PageWaterView water;

				/// @brief OuterVerticesPerPageSide^2 outer terrain heights, page-local
				///			(index x + z * OuterVerticesPerPageSide). Only read for PageState::Prepared.
				///			May be null, in which case ocean quads encode full depth.
				const float* heights{ nullptr };
			};

			/// @brief Encodes the water depth below the sea level into the G channel.
			/// @param seaLevel World Y of the ocean surface.
			/// @param terrainHeight World Y of the terrain under the texel.
			/// @return 0 at or above the sea level, 255 at DepthEncodeRange or more below it.
			[[nodiscard]] inline uint8 EncodeDepth(const float seaLevel, const float terrainHeight)
			{
				const float normalized = std::clamp((seaLevel - terrainHeight) / DepthEncodeRange, 0.0f, 1.0f);
				return static_cast<uint8>(normalized * 255.0f + 0.5f);
			}

			/// @brief Computes the mask texels of one water quad of a page.
			/// @param page The page view.
			/// @param localQuadX Quad X within the page, [0, QuadsPerPageSide).
			/// @param localQuadZ Quad Z within the page, [0, QuadsPerPageSide).
			/// @param seaLevel World Y of the ocean surface.
			/// @param outR Receives the R channel.
			/// @param outG Receives the G channel.
			inline void EncodeQuad(const PageSource& page, const uint32 localQuadX, const uint32 localQuadZ, const float seaLevel, uint8& outR, uint8& outG)
			{
				if (page.state != PageState::Prepared)
				{
					// Placeholder pages are open ocean; unavailable pages are assumed to be.
					outR = OceanDrawn;
					outG = 255;
					return;
				}

				const uint32 tileX = localQuadX / water_lookup::QuadsPerTileSide;
				const uint32 tileZ = localQuadZ / water_lookup::QuadsPerTileSide;
				const uint32 qx = localQuadX % water_lookup::QuadsPerTileSide;
				const uint32 qz = localQuadZ % water_lookup::QuadsPerTileSide;

				// Presence is the quad mask; the type byte outlives erased quads (see water_lookup).
				if (!water_lookup::HasWaterAtQuad(page.water, tileX, tileZ, qx, qz) ||
					water_lookup::TypeAtTile(page.water, tileX, tileZ) != WaterType::Ocean)
				{
					outR = 0;
					outG = 0;
					return;
				}

				outR = OceanDrawn;
				if (page.heights == nullptr)
				{
					outG = 255;
					return;
				}

				// The water quad and the terrain cell share their four outer corner vertices.
				constexpr uint32 side = constants::OuterVerticesPerPageSide;
				const size_t i00 = static_cast<size_t>(localQuadX) + static_cast<size_t>(localQuadZ) * side;
				const float height = (page.heights[i00] + page.heights[i00 + 1] + page.heights[i00 + side] + page.heights[i00 + side + 1]) * 0.25f;
				outG = EncodeDepth(seaLevel, height);
			}

			/// @brief Builds a camera-centred ocean mask window.
			/// @tparam ResolvePage Callable `PageSource(uint32 pageX, uint32 pageZ)`, only invoked for
			///			page indices inside [0, pagesX) x [0, pagesZ).
			/// @param centerX World X the window is centred on.
			/// @param centerZ World Z the window is centred on.
			/// @param texelsPerSide Window size in texels (= water quads) per side.
			/// @param pagesX Terrain width in pages. Page x spans world X [(x - pagesX / 2) * PageSize, +PageSize).
			/// @param pagesZ Terrain height in pages.
			/// @param seaLevel World Y of the ocean surface.
			/// @param resolvePage Resolves a page index to its view.
			/// @param outRG Receives texelsPerSide^2 * ChannelsPerTexel bytes (resized).
			/// @param outOriginX Receives the world X of the window's minimum corner (a quad boundary).
			/// @param outOriginZ Receives the world Z of the window's minimum corner (a quad boundary).
			template <typename ResolvePage>
			void Build(const float centerX, const float centerZ, const uint32 texelsPerSide, const uint32 pagesX, const uint32 pagesZ,
				const float seaLevel, const ResolvePage& resolvePage, std::vector<uint8>& outRG, float& outOriginX, float& outOriginZ)
			{
				// Global quad indices count from the terrain's minimum corner; world 0 is a page
				// boundary, so it is also a quad boundary and the snap below is exact there.
				const int64 halfQuadsX = static_cast<int64>(pagesX / 2) * QuadsPerPageSide;
				const int64 halfQuadsZ = static_cast<int64>(pagesZ / 2) * QuadsPerPageSide;
				const int64 totalQuadsX = static_cast<int64>(pagesX) * QuadsPerPageSide;
				const int64 totalQuadsZ = static_cast<int64>(pagesZ) * QuadsPerPageSide;

				const int64 firstQuadX = static_cast<int64>(std::floor(static_cast<double>(centerX) / TexelSize)) + halfQuadsX - static_cast<int64>(texelsPerSide / 2);
				const int64 firstQuadZ = static_cast<int64>(std::floor(static_cast<double>(centerZ) / TexelSize)) + halfQuadsZ - static_cast<int64>(texelsPerSide / 2);

				outOriginX = static_cast<float>(static_cast<double>(firstQuadX - halfQuadsX) * TexelSize);
				outOriginZ = static_cast<float>(static_cast<double>(firstQuadZ - halfQuadsZ) * TexelSize);

				outRG.resize(static_cast<size_t>(texelsPerSide) * texelsPerSide * ChannelsPerTexel);

				// Texels run along page rows, so the page view is resolved once per run.
				int64 cachedPageX = -1;
				int64 cachedPageZ = -1;
				PageSource cachedPage;

				for (uint32 tz = 0; tz < texelsPerSide; ++tz)
				{
					const int64 gz = firstQuadZ + tz;
					uint8* row = outRG.data() + static_cast<size_t>(tz) * texelsPerSide * ChannelsPerTexel;

					for (uint32 tx = 0; tx < texelsPerSide; ++tx)
					{
						const int64 gx = firstQuadX + tx;
						uint8* texel = row + static_cast<size_t>(tx) * ChannelsPerTexel;

						if (gx < 0 || gz < 0 || gx >= totalQuadsX || gz >= totalQuadsZ)
						{
							// Off the map: open ocean to the horizon.
							texel[0] = OceanDrawn;
							texel[1] = 255;
							continue;
						}

						const int64 pageX = gx / QuadsPerPageSide;
						const int64 pageZ = gz / QuadsPerPageSide;
						if (pageX != cachedPageX || pageZ != cachedPageZ)
						{
							cachedPageX = pageX;
							cachedPageZ = pageZ;
							cachedPage = resolvePage(static_cast<uint32>(pageX), static_cast<uint32>(pageZ));
						}

						EncodeQuad(cachedPage, static_cast<uint32>(gx % QuadsPerPageSide), static_cast<uint32>(gz % QuadsPerPageSide), seaLevel, texel[0], texel[1]);
					}
				}
			}
		}
	}
}
