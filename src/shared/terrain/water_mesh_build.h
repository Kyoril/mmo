// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain/constants.h"
#include "terrain/water_lookup.h"

#include <algorithm>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		/// @brief Pure grouping of a page's water sub-quads for mesh construction.
		///
		/// @remark Dependency-free for the same reason as water_lookup: terrain_tests links only
		///			base and math.
		namespace water_mesh
		{
			/// @brief The address of one water sub-quad within a page.
			struct QuadRef
			{
				uint32 tileX{ 0 };
				uint32 tileZ{ 0 };
				uint32 qx{ 0 };
				uint32 qz{ 0 };
			};

			/// @brief Every sub-quad of a page sharing one liquid type.
			/// @remark One render operation is emitted per batch, which is what lets a single page
			///			carry ocean and lake side by side with different materials.
			struct TypeBatch
			{
				WaterType type{ WaterType::None };
				std::vector<QuadRef> quads;
			};

			/// @brief Vertex colour alpha written to top-face water vertices.
			constexpr uint32 TopFaceVertexAlpha = 0xFFu;

			/// @brief Vertex colour alpha written to bottom-face water vertices.
			/// @remark The material graph compares vertex colour alpha against a midpoint to switch
			///			to the underside look (darker, silvery, inverted Fresnel) when the surface is
			///			seen from below. Carrying face identity in vertex data avoids plumbing a
			///			front-facing semantic through the material compiler and both backends.
			constexpr uint32 BottomFaceVertexAlpha = 0x00u;

			/// @brief The deepest water, in world units, a water vertex colour can represent.
			/// @remark The water material decodes vertex colour red with this same range
			///			(tools/water_gen/build_ocean_material.py); change both together.
			constexpr float ShoreDepthEncodeRange = 8.0f;

			/// @brief Packs how deep the water is over the terrain into one byte.
			/// @param waterHeight World Y of the water surface at the vertex.
			/// @param terrainHeight World Y of the terrain at the same vertex.
			/// @return 0 for terrain at or above the surface (the shoreline itself), 255 for water at
			///			least ShoreDepthEncodeRange deep.
			/// @remark Shore foam used to key off the screen-space distance between the surface and
			///			whatever the opaque pass drew behind it. That distance is equally small for a
			///			character's body just under the surface, so every swimmer was wrapped in
			///			shoreline foam. The terrain depth baked here only knows about the terrain.
			[[nodiscard]] inline uint32 EncodeShoreDepth(const float waterHeight, const float terrainHeight)
			{
				const float depth = std::clamp(waterHeight - terrainHeight, 0.0f, ShoreDepthEncodeRange);
				return static_cast<uint32>(depth / ShoreDepthEncodeRange * 255.0f + 0.5f);
			}

			/// @brief Builds a water vertex colour: the face tag in alpha, the shore depth in red.
			/// @param faceAlpha TopFaceVertexAlpha or BottomFaceVertexAlpha.
			/// @param waterHeight World Y of the water surface at the vertex.
			/// @param terrainHeight World Y of the terrain at the same vertex.
			/// @return The ARGB colour. Green and blue stay white; nothing reads them.
			[[nodiscard]] inline uint32 MakeWaterVertexColor(const uint32 faceAlpha, const float waterHeight, const float terrainHeight)
			{
				return (faceAlpha << 24) | (EncodeShoreDepth(waterHeight, terrainHeight) << 16) | 0x0000FFFFu;
			}

			/// @brief Decides whether explicit reversed-winding (underside) triangles are emitted.
			/// @param materialTwoSided Whether the batch's resolved material is two-sided.
			/// @return False for a two-sided material, true otherwise.
			/// @remark A two-sided material disables culling, so underside triangles would also
			///			rasterise when viewed from above: drawn after the top face, lit by their
			///			downward normal, and blended over the lit surface - which renders the water
			///			black. The material already covers both sides, so the geometry is redundant
			///			and doubles translucent overdraw. A single-sided material culls per face, so
			///			each winding is only visible from its own side and the underside triangles are
			///			what make the surface visible from below.
			[[nodiscard]] inline bool ShouldEmitBottomFaces(const bool materialTwoSided)
			{
				return !materialTwoSided;
			}

			/// @brief Groups every water-carrying sub-quad of a page by its tile's liquid type.
			/// @param view The page's water arrays.
			/// @return One batch per distinct liquid type actually present, ordered by ascending
			///			WaterType. Tiles whose quads have all been erased contribute nothing, so a
			///			caller never builds a render operation with no triangles.
			inline std::vector<TypeBatch> BucketQuadsByType(const water_lookup::PageWaterView& view)
			{
				std::vector<TypeBatch> batches;

				if (view.quadMasks == nullptr || view.types == nullptr)
				{
					return batches;
				}

				for (uint32 tz = 0; tz < constants::TilesPerPage; ++tz)
				{
					for (uint32 tx = 0; tx < constants::TilesPerPage; ++tx)
					{
						const uint64 mask = view.quadMasks[tx + tz * constants::TilesPerPage];
						if (mask == 0ULL)
						{
							continue;
						}

						const WaterType type = water_lookup::TypeAtTile(view, tx, tz);

						auto it = std::find_if(batches.begin(), batches.end(),
							[type](const TypeBatch& batch) { return batch.type == type; });
						if (it == batches.end())
						{
							batches.push_back(TypeBatch{ type, {} });
							it = batches.end() - 1;
						}

						for (uint32 qz = 0; qz < water_lookup::QuadsPerTileSide; ++qz)
						{
							for (uint32 qx = 0; qx < water_lookup::QuadsPerTileSide; ++qx)
							{
								if ((mask & (1ULL << (qx + qz * water_lookup::QuadsPerTileSide))) == 0ULL)
								{
									continue;
								}

								it->quads.push_back(QuadRef{ tx, tz, qx, qz });
							}
						}
					}
				}

				// Deterministic order. Page render objects are destroyed and rebuilt on every
				// stream-in, and an unstable batch order would shuffle translucent draw order from
				// one visit to the next.
				std::sort(batches.begin(), batches.end(),
					[](const TypeBatch& lhs, const TypeBatch& rhs)
					{
						return static_cast<uint8>(lhs.type) < static_cast<uint8>(rhs.type);
					});

				return batches;
			}
		}
	}
}
