// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "import_command.h"

#include "image_io.h"
#include "terrain_pages.h"
#include "zone_meta.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "terrain_io/page_io.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <set>

namespace mmo
{
	namespace
	{
		constexpr uint32 OuterSide = terrain::constants::OuterVerticesPerPageSide;	// 129
		constexpr uint32 InnerSide = terrain::constants::InnerVerticesPerPageSide;	// 128
		constexpr uint32 VerticesPerPageEdge = OuterSide - 1;						// 128
		constexpr uint32 TilesPerPage = terrain::constants::TilesPerPage;			// 16

		/// @brief Samples the heightmap image bilinearly at normalized zone coordinates.
		float SampleHeight(const GrayImage16 &image, const ZoneMeta &meta, const double u, const double v)
		{
			const double px = u * static_cast<double>(image.width - 1);
			const double pz = v * static_cast<double>(image.height - 1);

			const uint32 x0 = static_cast<uint32>(px);
			const uint32 z0 = static_cast<uint32>(pz);
			const uint32 x1 = (x0 + 1 < image.width) ? x0 + 1 : x0;
			const uint32 z1 = (z0 + 1 < image.height) ? z0 + 1 : z0;
			const double fx = px - static_cast<double>(x0);
			const double fz = pz - static_cast<double>(z0);

			const double p00 = image.pixels[static_cast<size_t>(z0) * image.width + x0];
			const double p10 = image.pixels[static_cast<size_t>(z0) * image.width + x1];
			const double p01 = image.pixels[static_cast<size_t>(z1) * image.width + x0];
			const double p11 = image.pixels[static_cast<size_t>(z1) * image.width + x1];

			const double value = p00 * (1.0 - fx) * (1.0 - fz) + p10 * fx * (1.0 - fz) + p01 * (1.0 - fx) * fz + p11 * fx * fz;
			return meta.minY + static_cast<float>(value / 65535.0) * (meta.maxY - meta.minY);
		}

		/// @brief Copies the zone ids of one page out of the zone map into a page's per-tile zone array.
		///	@param onlyUnset When true, tiles that already carry a zone keep it.
		///	@return Number of tiles whose zone changed.
		uint32 ApplyZones(const GrayImage16 &zoneMap, const ZoneMeta &meta, const int32 pageX, const int32 pageZ, std::vector<uint32> &zones, const bool onlyUnset)
		{
			uint32 changed = 0;
			for (uint32 tileZ = 0; tileZ < TilesPerPage; ++tileZ)
			{
				for (uint32 tileX = 0; tileX < TilesPerPage; ++tileX)
				{
					const uint32 mapX = static_cast<uint32>(pageX - meta.pageX0) * TilesPerPage + tileX;
					const uint32 mapZ = static_cast<uint32>(pageZ - meta.pageZ0) * TilesPerPage + tileZ;
					const uint32 zone = zoneMap.pixels[static_cast<size_t>(mapZ) * zoneMap.width + mapX];

					uint32 &target = zones[tileX + tileZ * TilesPerPage];
					if (zone == 0 || target == zone || (onlyUnset && target != 0))
					{
						continue;
					}

					target = zone;
					++changed;
				}
			}

			return changed;
		}

		bool WritePage(const String &filename, const terrain_io::PageData &page)
		{
			const auto file = AssetRegistry::CreateNewFile(filename);
			if (!file)
			{
				ELOG("Failed to create terrain page file '" << filename << "'!");
				return false;
			}

			io::StreamSink sink{ *file };
			io::Writer writer{ sink };
			if (!terrain_io::SavePage(writer, terrain_io::MakeView(page)))
			{
				ELOG("Failed to serialize terrain page '" << filename << "'!");
				return false;
			}

			sink.Flush();
			return true;
		}

		/// @brief Flow coordinate of a point relative to a river path: distance downstream along the
		///	       path, signed distance across it (positive to the left of the flow seen from above, so
		///	       the mapping is a rotation of the world mapping), and the channel half width there.
		struct FlowCoordinate
		{
			float along = 0.0f;
			float across = 0.0f;
			float halfWidth = 0.0f;
		};

		FlowCoordinate ProjectOntoFlowPath(const std::vector<ZoneMeta::FlowPoint> &path, const float x, const float z)
		{
			FlowCoordinate best;
			float bestDistance = std::numeric_limits<float>::max();
			float start = 0.0f;
			for (size_t i = 0; i + 1 < path.size(); ++i)
			{
				const ZoneMeta::FlowPoint &a = path[i];
				const ZoneMeta::FlowPoint &b = path[i + 1];
				const float dx = b.x - a.x;
				const float dz = b.z - a.z;
				const float length = std::sqrt(dx * dx + dz * dz);
				if (length <= 0.0f)
				{
					continue;
				}

				const float t = std::clamp(((x - a.x) * dx + (z - a.z) * dz) / (length * length), 0.0f, 1.0f);
				const float px = a.x + dx * t;
				const float pz = a.z + dz * t;
				const float distance = std::hypot(x - px, z - pz);
				if (distance < bestDistance)
				{
					bestDistance = distance;
					best.along = start + t * length;
					// Left of the flow direction (dx, dz) is (-dz, dx): for flow along +X that is +Z,
					// matching v = z of the world mapping.
					best.across = ((x - px) * -dz + (z - pz) * dx) / length;
					best.halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * t;
				}
				start += length;
			}

			return best;
		}

		/// @brief Loads an existing terrain page.
		bool LoadExistingPage(const String &filename, terrain_io::PageData &page)
		{
			const auto file = AssetRegistry::OpenFile(filename);
			if (!file)
			{
				ELOG("Failed to open existing terrain page '" << filename << "'!");
				return false;
			}

			io::StreamSource source{ *file };
			io::Reader reader{ source };
			if (!terrain_io::LoadPage(reader, page))
			{
				ELOG("Failed to load existing terrain page '" << filename << "'!");
				return false;
			}

			return true;
		}

		/// @brief Adds a height offset grid to the existing pages of the rect and writes them back,
		///	       keeping everything but heights, the normals of changed vertices and new water quads.
		/// @param delta Height offsets on the continuous outer vertex grid of the rect.
		int32 ApplyHeightDelta(const ZoneMeta &meta, const std::vector<float> &delta, const uint32 gridWidth, const uint32 gridHeight,
			const GrayImage16 &zoneMap, const std::set<std::pair<int32, int32>> &pageFilter)
		{
			ZoneGrid existing;
			if (!LoadZoneGrid(meta.world, meta.pageX0, meta.pageZ0, meta.pageX1, meta.pageZ1, existing))
			{
				return 1;
			}

			const uint32 pageCount = static_cast<uint32>(meta.GetPageCountX() * meta.GetPageCountZ());
			if (existing.loadedPageCount != pageCount || existing.width != gridWidth || existing.height != gridHeight)
			{
				ELOG("A delta import needs every page of the rect to exist (" << existing.loadedPageCount << " of " << pageCount << " found)");
				return 1;
			}

			constexpr uint32 splatSide = terrain::constants::PixelsPerPage;
			const uint32 splatWidth = static_cast<uint32>(meta.GetPageCountX()) * (splatSide - 1) + 1;
			const uint32 splatHeight = static_cast<uint32>(meta.GetPageCountZ()) * (splatSide - 1) + 1;
			std::vector<GrayImage16> paintMasks(meta.paint.size());
			for (size_t i = 0; i < meta.paint.size(); ++i)
			{
				if (!LoadGray16Png(meta.paint[i].mask, paintMasks[i]))
				{
					return 1;
				}
				if (paintMasks[i].width != splatWidth || paintMasks[i].height != splatHeight)
				{
					ELOG("Paint mask '" << meta.paint[i].mask.string() << "' is " << paintMasks[i].width << "x" << paintMasks[i].height
						<< " but must be " << splatWidth << "x" << splatHeight);
					return 1;
				}
			}

			GrayImage16 tileMaterialMask;
			if (!meta.tileMaterialMask.empty())
			{
				if (!LoadGray16Png(meta.tileMaterialMask, tileMaterialMask))
				{
					return 1;
				}
				if (tileMaterialMask.width != static_cast<uint32>(meta.GetPageCountX()) * TilesPerPage ||
					tileMaterialMask.height != static_cast<uint32>(meta.GetPageCountZ()) * TilesPerPage)
				{
					ELOG("Tile material mask must hold one pixel per tile");
					return 1;
				}
			}

			GrayImage16 clearWater;
			if (!meta.clearWaterMask.empty())
			{
				if (!LoadGray16Png(meta.clearWaterMask, clearWater))
				{
					return 1;
				}
				const uint32 quadsWide = static_cast<uint32>(meta.GetPageCountX()) * VerticesPerPageEdge;
				const uint32 quadsHigh = static_cast<uint32>(meta.GetPageCountZ()) * VerticesPerPageEdge;
				if (clearWater.width != quadsWide || clearWater.height != quadsHigh)
				{
					ELOG("Clear-water mask must hold one pixel per water quad (" << quadsWide << "x" << quadsHigh << ")");
					return 1;
				}
			}

			// Offsets this small are quantization noise of a zero delta.
			constexpr float changeEpsilon = 0.0005f;

			std::vector<float> heights(existing.heights.size());
			std::vector<uint8> changed(existing.heights.size(), 0);
			for (size_t i = 0; i < heights.size(); ++i)
			{
				heights[i] = existing.heights[i] + delta[i];
				changed[i] = std::abs(delta[i]) > changeEpsilon ? 1 : 0;
			}

			// A vertex normal depends on its four neighbours, so it is recomputed when any of them moved.
			// Everywhere else the stored normal stays bit for bit, including at the rect border where
			// the neighbours outside the rect are unknown here.
			const auto normalChanged = [&](const uint32 x, const uint32 z)
			{
				for (int32 dz = -1; dz <= 1; ++dz)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						const int32 nx = static_cast<int32>(x) + dx;
						const int32 nz = static_cast<int32>(z) + dz;
						if (nx < 0 || nz < 0 || nx >= static_cast<int32>(gridWidth) || nz >= static_cast<int32>(gridHeight))
						{
							continue;
						}
						if (changed[static_cast<size_t>(nz) * gridWidth + nx])
						{
							return true;
						}
					}
				}
				return false;
			};

			// River flow coordinates for the whole rect at once, then smoothed: the nearest path segment
			// switches on the inside of bends, which makes "along" jump by a little, and every jump
			// shows as a seam in the panned ripples. The rect-wide grid also keeps page borders exact.
			std::vector<float> flowAlong;
			std::vector<float> flowAcross;
			std::vector<float> flowSpeed;
			if (meta.flowPath.size() >= 2)
			{
				const float vertexSpacing = terrain::constants::PageSize / static_cast<float>(VerticesPerPageEdge);
				const float originX = static_cast<float>(meta.pageX0 - 32) * terrain::constants::PageSize;
				const float originZ = static_cast<float>(meta.pageZ0 - 32) * terrain::constants::PageSize;
				const size_t count = static_cast<size_t>(gridWidth) * gridHeight;
				flowAlong.resize(count);
				flowAcross.resize(count);
				flowSpeed.resize(count);
				for (uint32 z = 0; z < gridHeight; ++z)
				{
					for (uint32 x = 0; x < gridWidth; ++x)
					{
						const FlowCoordinate c = ProjectOntoFlowPath(meta.flowPath,
							originX + static_cast<float>(x) * vertexSpacing, originZ + static_cast<float>(z) * vertexSpacing);
						const size_t i = static_cast<size_t>(z) * gridWidth + x;
						flowAlong[i] = c.along;
						flowAcross[i] = c.across;
						const float beyondBank = std::max(0.0f, std::abs(c.across) - c.halfWidth);
						flowSpeed[i] = std::clamp(1.0f - beyondBank / std::max(meta.flowFalloff, 1.0f), meta.flowMinSpeed, 1.0f);
					}
				}

				// Three separable box blurs of radius 3 approximate a Gaussian of about 3 vertices (12 m).
				const auto blur = [&](std::vector<float> &values)
				{
					std::vector<float> scratch(values.size());
					constexpr int32 radius = 3;
					for (int pass = 0; pass < 3; ++pass)
					{
						for (uint32 z = 0; z < gridHeight; ++z)
						{
							for (uint32 x = 0; x < gridWidth; ++x)
							{
								float sum = 0.0f;
								for (int32 k = -radius; k <= radius; ++k)
								{
									const int32 sx = std::clamp(static_cast<int32>(x) + k, 0, static_cast<int32>(gridWidth) - 1);
									sum += values[static_cast<size_t>(z) * gridWidth + sx];
								}
								scratch[static_cast<size_t>(z) * gridWidth + x] = sum / static_cast<float>(radius * 2 + 1);
							}
						}
						for (uint32 z = 0; z < gridHeight; ++z)
						{
							for (uint32 x = 0; x < gridWidth; ++x)
							{
								float sum = 0.0f;
								for (int32 k = -radius; k <= radius; ++k)
								{
									const int32 sz = std::clamp(static_cast<int32>(z) + k, 0, static_cast<int32>(gridHeight) - 1);
									sum += scratch[static_cast<size_t>(sz) * gridWidth + x];
								}
								values[static_cast<size_t>(z) * gridWidth + x] = sum / static_cast<float>(radius * 2 + 1);
							}
						}
					}
				};
				blur(flowAlong);
				blur(flowAcross);
				blur(flowSpeed);
			}

			uint32 savedPages = 0;
			uint32 changedVertices = 0;
			uint32 addedWaterQuads = 0;
			uint32 removedWaterQuads = 0;
			terrain_io::PageData page;
			for (int32 pageZ = meta.pageZ0; pageZ <= meta.pageZ1; ++pageZ)
			{
				for (int32 pageX = meta.pageX0; pageX <= meta.pageX1; ++pageX)
				{
					if (!pageFilter.empty() && !pageFilter.contains({ pageX, pageZ }))
					{
						continue;
					}

					const String filename = BuildPageFilename(meta.world, pageX, pageZ);
					if (!LoadExistingPage(filename, page))
					{
						return 1;
					}

					const uint32 baseX = static_cast<uint32>(pageX - meta.pageX0) * VerticesPerPageEdge;
					const uint32 baseZ = static_cast<uint32>(pageZ - meta.pageZ0) * VerticesPerPageEdge;
					bool pageChanged = false;

					std::vector<uint8> outerNormalChanged(static_cast<size_t>(OuterSide) * OuterSide, 0);
					std::vector<Vector3> outerNormals(static_cast<size_t>(OuterSide) * OuterSide);
					for (uint32 z = 0; z < OuterSide; ++z)
					{
						for (uint32 x = 0; x < OuterSide; ++x)
						{
							const uint32 gx = baseX + x;
							const uint32 gz = baseZ + z;
							const size_t gridIndex = static_cast<size_t>(gz) * gridWidth + gx;
							const size_t pageIndex = static_cast<size_t>(z) * OuterSide + x;

							if (changed[gridIndex])
							{
								page.heightmap[pageIndex] = heights[gridIndex];
								++changedVertices;
								pageChanged = true;
							}

							if (normalChanged(gx, gz))
							{
								const Vector3 normal = ComputeOuterNormal(heights, gridWidth, gridHeight, gx, gz);
								outerNormals[pageIndex] = normal;
								outerNormalChanged[pageIndex] = 1;
								page.normals[pageIndex] = EncodeNormalSNorm8(normal.x, normal.y, normal.z);
							}
							else
							{
								DecodeNormalSNorm8(page.normals[pageIndex], outerNormals[pageIndex].x, outerNormals[pageIndex].y, outerNormals[pageIndex].z);
							}
						}
					}

					// Paint: blend each layer's weight towards the mask, keeping the four weights summing to 255.
					for (size_t i = 0; i < meta.paint.size(); ++i)
					{
						const GrayImage16 &mask = paintMasks[i];
						const uint32 target = meta.paint[i].layer;
						const uint32 maskX = static_cast<uint32>(pageX - meta.pageX0) * (splatSide - 1);
						const uint32 maskZ = static_cast<uint32>(pageZ - meta.pageZ0) * (splatSide - 1);
						for (uint32 z = 0; z < splatSide; ++z)
						{
							for (uint32 x = 0; x < splatSide; ++x)
							{
								const float weight = std::clamp(meta.paint[i].strength *
									static_cast<float>(mask.pixels[static_cast<size_t>(maskZ + z) * mask.width + maskX + x]) / 65535.0f, 0.0f, 1.0f);
								if (weight <= 0.0f)
								{
									continue;
								}

								uint32 &packed = page.layers[static_cast<size_t>(z) * splatSide + x];
								if (meta.paint[i].remove)
								{
									// Move this layer's weight onto layer 0 (grass).
									uint32 b[4] = { packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF, packed >> 24 };
									const uint32 moved = static_cast<uint32>(std::lround(static_cast<float>(b[target]) * weight));
									if (moved == 0 || target == 0)
									{
										continue;
									}
									b[target] -= moved;
									b[0] += moved;
									packed = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
									pageChanged = true;
									continue;
								}

								uint32 bytes[4];
								uint32 others = 0;
								for (uint32 c = 0; c < 4; ++c)
								{
									const float old = static_cast<float>((packed >> (c * 8)) & 0xFF);
									const float blended = old * (1.0f - weight) + (c == target ? 255.0f * weight : 0.0f);
									bytes[c] = static_cast<uint32>(std::lround(blended));
									if (c != target)
									{
										others += bytes[c];
									}
								}
								// Rounding can push the other layers one over 255 when the weight is tiny.
								while (others > 255)
								{
									uint32 largest = target == 0 ? 1 : 0;
									for (uint32 c = 0; c < 4; ++c)
									{
										if (c != target && bytes[c] > bytes[largest])
										{
											largest = c;
										}
									}
									--bytes[largest];
									--others;
								}
								bytes[target] = 255 - others;

								const uint32 repacked = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
								if (repacked != packed)
								{
									packed = repacked;
									pageChanged = true;
								}
							}
						}
					}

					// River flow: every vertex of a page with water gets its flow-aligned UV and speed.
					if (meta.flowPath.size() >= 2 && std::any_of(page.waterQuadMasks.begin(), page.waterQuadMasks.end(), [](const uint64 mask) { return mask != 0; }))
					{
						page.waterFlowUVs.assign(static_cast<size_t>(OuterSide) * OuterSide * 2, 0.0f);
						page.waterFlowSpeeds.assign(static_cast<size_t>(OuterSide) * OuterSide, 0);
						for (uint32 vz = 0; vz < OuterSide; ++vz)
						{
							for (uint32 vx = 0; vx < OuterSide; ++vx)
							{
								const size_t g = static_cast<size_t>(baseZ + vz) * gridWidth + (baseX + vx);
								const size_t index = static_cast<size_t>(vz) * OuterSide + vx;
								page.waterFlowUVs[index * 2] = flowAlong[g] * meta.flowUVScale;
								page.waterFlowUVs[index * 2 + 1] = flowAcross[g] * meta.flowUVScale;
								page.waterFlowSpeeds[index] = static_cast<uint8>(std::lround(std::clamp(flowSpeed[g], 0.0f, 1.0f) * 255.0f));
							}
						}
						pageChanged = true;
					}

					// Per-tile materials
					if (!tileMaterialMask.pixels.empty())
					{
						for (uint32 tileZ = 0; tileZ < TilesPerPage; ++tileZ)
						{
							for (uint32 tileX = 0; tileX < TilesPerPage; ++tileX)
							{
								const uint32 mx = static_cast<uint32>(pageX - meta.pageX0) * TilesPerPage + tileX;
								const uint32 mz = static_cast<uint32>(pageZ - meta.pageZ0) * TilesPerPage + tileZ;
								const uint32 k = tileMaterialMask.pixels[static_cast<size_t>(mz) * tileMaterialMask.width + mx];
								if (k == 0 || k > meta.tileMaterials.size())
								{
									continue;
								}
								String &name = page.materialNames[tileX + tileZ * TilesPerPage];
								if (name != meta.tileMaterials[k - 1])
								{
									name = meta.tileMaterials[k - 1];
									pageChanged = true;
								}
							}
						}
					}

					// Remove water from the quads the clear-water mask marks, wherever it came from.
					if (!clearWater.pixels.empty())
					{
						constexpr uint32 quadsPerTileSide = 8;
						for (uint32 quadZ = 0; quadZ < VerticesPerPageEdge; ++quadZ)
						{
							for (uint32 quadX = 0; quadX < VerticesPerPageEdge; ++quadX)
							{
								if (clearWater.pixels[static_cast<size_t>(baseZ + quadZ) * clearWater.width + baseX + quadX] < 32768)
								{
									continue;
								}

								const uint32 tileIndex = quadX / quadsPerTileSide + (quadZ / quadsPerTileSide) * TilesPerPage;
								const uint64 bit = uint64(1) << (quadX % quadsPerTileSide + (quadZ % quadsPerTileSide) * quadsPerTileSide);
								if (page.waterQuadMasks[tileIndex] & bit)
								{
									page.waterQuadMasks[tileIndex] &= ~bit;
									if (page.waterQuadMasks[tileIndex] == 0)
									{
										page.waterTypes[tileIndex] = 0;
									}
									++removedWaterQuads;
									pageChanged = true;
								}
							}
						}
					}

					if (meta.setWaterMaterial && page.waterMaterialName != meta.waterMaterial &&
						std::any_of(page.waterQuadMasks.begin(), page.waterQuadMasks.end(), [](const uint64 mask) { return mask != 0; }))
					{
						pageChanged = true;
					}

					if (!pageChanged)
					{
						continue;
					}

					// Inner vertices sit at the centre of each outer quad: they move by the mean offset of
					// the quad's corners, so sculpted inner detail is kept, and take the engine's mean of
					// the four corner normals.
					for (uint32 j = 0; j < InnerSide; ++j)
					{
						for (uint32 i = 0; i < InnerSide; ++i)
						{
							const size_t g00 = static_cast<size_t>(baseZ + j) * gridWidth + (baseX + i);
							const size_t p00 = static_cast<size_t>(j) * OuterSide + i;
							const size_t innerIndex = static_cast<size_t>(j) * InnerSide + i;

							const float meanOffset = (delta[g00] + delta[g00 + 1] + delta[g00 + gridWidth] + delta[g00 + gridWidth + 1]) * 0.25f;
							if (std::abs(meanOffset) > changeEpsilon)
							{
								page.innerHeightmap[innerIndex] += meanOffset;
							}

							if (outerNormalChanged[p00] || outerNormalChanged[p00 + 1] || outerNormalChanged[p00 + OuterSide] || outerNormalChanged[p00 + OuterSide + 1])
							{
								const Vector3 average = ((outerNormals[p00] + outerNormals[p00 + 1] +
									outerNormals[p00 + OuterSide] + outerNormals[p00 + OuterSide + 1]) * 0.25f).NormalizedCopy();
								page.innerNormals[innerIndex] = EncodeNormalSNorm8(average.x, average.y, average.z);
							}
						}
					}

					// Water of reshaped quads follows the new ground: a reshaped quad holds water exactly
					// where it dips below the level. Quads the delta left alone keep their water as it is,
					// and water heights still used by untouched quads are not moved.
					if (meta.HasWaterLevel())
					{
						constexpr uint32 quadsPerTileSide = 8;
						const auto quadChanged = [&](const uint32 vertX, const uint32 vertZ)
						{
							const size_t g00 = static_cast<size_t>(baseZ + vertZ) * gridWidth + (baseX + vertX);
							return changed[g00] || changed[g00 + 1] || changed[g00 + gridWidth] || changed[g00 + gridWidth + 1];
						};

						std::vector<uint8> vertexKept(static_cast<size_t>(OuterSide) * OuterSide, 0);
						for (uint32 tileIndex = 0; tileIndex < TilesPerPage * TilesPerPage; ++tileIndex)
						{
							const uint64 mask = page.waterQuadMasks[tileIndex];
							for (uint32 quad = 0; mask != 0 && quad < 64; ++quad)
							{
								const uint32 vertX = (tileIndex % TilesPerPage) * quadsPerTileSide + quad % quadsPerTileSide;
								const uint32 vertZ = (tileIndex / TilesPerPage) * quadsPerTileSide + quad / quadsPerTileSide;
								if (((mask >> quad) & 1) && !quadChanged(vertX, vertZ))
								{
									vertexKept[vertZ * OuterSide + vertX] = 1;
									vertexKept[vertZ * OuterSide + vertX + 1] = 1;
									vertexKept[(vertZ + 1) * OuterSide + vertX] = 1;
									vertexKept[(vertZ + 1) * OuterSide + vertX + 1] = 1;
								}
							}
						}

						for (uint32 tileZ = 0; tileZ < TilesPerPage; ++tileZ)
						{
							for (uint32 tileX = 0; tileX < TilesPerPage; ++tileX)
							{
								const uint32 tileIndex = tileX + tileZ * TilesPerPage;
								const bool hadWater = page.waterQuadMasks[tileIndex] != 0;
								for (uint32 quadZ = 0; quadZ < quadsPerTileSide; ++quadZ)
								{
									for (uint32 quadX = 0; quadX < quadsPerTileSide; ++quadX)
									{
										const uint32 vertX = tileX * quadsPerTileSide + quadX;
										const uint32 vertZ = tileZ * quadsPerTileSide + quadZ;
										if (!quadChanged(vertX, vertZ))
										{
											continue;
										}

										const uint64 bit = uint64(1) << (quadX + quadZ * quadsPerTileSide);
										const size_t c00 = static_cast<size_t>(vertZ) * OuterSide + vertX;
										const float lowest = std::min(std::min(page.heightmap[c00], page.heightmap[c00 + 1]),
											std::min(page.heightmap[c00 + OuterSide], page.heightmap[c00 + OuterSide + 1]));
										const bool wantWater = lowest < meta.waterLevel;
										const bool hasWater = (page.waterQuadMasks[tileIndex] & bit) != 0;
										if (wantWater == hasWater)
										{
											continue;
										}

										if (wantWater)
										{
											page.waterQuadMasks[tileIndex] |= bit;
											for (const size_t corner : { c00, c00 + 1, c00 + OuterSide, c00 + OuterSide + 1 })
											{
												if (!vertexKept[corner])
												{
													page.waterVertexHeights[corner] = meta.waterLevel;
												}
											}
											++addedWaterQuads;
										}
										else
										{
											page.waterQuadMasks[tileIndex] &= ~bit;
											++removedWaterQuads;
										}
									}
								}

								if (!hadWater && page.waterQuadMasks[tileIndex] != 0)
								{
									page.waterTypes[tileIndex] = meta.waterType;
								}
								else if (page.waterQuadMasks[tileIndex] == 0)
								{
									page.waterTypes[tileIndex] = 0;
								}
							}
						}
					}

					// Optionally give every reshaped page that holds water the configured surface material.
					if (meta.setWaterMaterial && std::any_of(page.waterQuadMasks.begin(), page.waterQuadMasks.end(), [](const uint64 mask) { return mask != 0; }))
					{
						page.waterMaterialName = meta.waterMaterial;
					}
					else if (page.waterMaterialName.empty() && std::any_of(page.waterQuadMasks.begin(), page.waterQuadMasks.end(), [](const uint64 mask) { return mask != 0; }))
					{
						page.waterMaterialName = meta.waterMaterial;
					}

					if (!zoneMap.pixels.empty())
					{
						ApplyZones(zoneMap, meta, pageX, pageZ, page.zones, false);
					}

					if (!WritePage(filename, page))
					{
						return 1;
					}

					++savedPages;
				}
			}

			ILOG("Delta import changed " << changedVertices << " vertices on " << savedPages << " pages" << (meta.paint.empty() ? String() : " (with " + std::to_string(meta.paint.size()) + " painted layers)")
				<< (meta.HasWaterLevel() || !clearWater.pixels.empty() ? ", water quads +" + std::to_string(addedWaterQuads) + " -" + std::to_string(removedWaterQuads) : String()));
			ILOG("Note: rebake the distant terrain (mmo_edit --bake-terrain-lod) and the nav mesh of the changed pages");
			return 0;
		}

		/// @brief Fills the unset zone ids of a page that already exists and is not overwritten.
		///	@return false if the page could not be read or written.
		bool FillZonesOfExistingPage(const String &filename, const GrayImage16 &zoneMap, const ZoneMeta &meta, const int32 pageX, const int32 pageZ, uint32 &changedTiles)
		{
			terrain_io::PageData page;
			{
				const auto file = AssetRegistry::OpenFile(filename);
				if (!file)
				{
					ELOG("Failed to open existing terrain page '" << filename << "'!");
					return false;
				}

				io::StreamSource source{ *file };
				io::Reader reader{ source };
				if (!terrain_io::LoadPage(reader, page))
				{
					ELOG("Failed to load existing terrain page '" << filename << "'!");
					return false;
				}
			}

			changedTiles = ApplyZones(zoneMap, meta, pageX, pageZ, page.zones, true);
			return changedTiles == 0 || WritePage(filename, page);
		}
	}

	int32 RunImport(const ImportArgs &args)
	{
		ZoneMeta meta;
		if (!LoadZoneMeta(args.metaPath, meta))
		{
			return 1;
		}

		if (!args.world.empty())
		{
			meta.world = args.world;
		}

		if (!args.materialOverride.empty())
		{
			meta.material = args.materialOverride;
		}

		if (!std::isnan(args.waterLevelOverride))
		{
			meta.waterLevel = args.waterLevelOverride;
		}

		if (!args.waterMaterialOverride.empty())
		{
			meta.waterMaterial = args.waterMaterialOverride;
		}

		if (!meta.IsValid())
		{
			return 1;
		}

		GrayImage16 image;
		if (!LoadGray16Png(args.heightmapPath, image))
		{
			return 1;
		}

		if (!AssetRegistry::HasFile(BuildWorldFilename(meta.world)))
		{
			WLOG("World file '" << BuildWorldFilename(meta.world) << "' does not exist. Create the world in the editor first (with terrain enabled and a default material), otherwise the imported terrain won't be usable!");
		}

		if (meta.HasWaterLevel() && !meta.waterMaterial.empty() && !AssetRegistry::HasFile(meta.waterMaterial))
		{
			WLOG("Water material '" << meta.waterMaterial << "' does not exist - water will render as wireframe until a valid material is assigned!");
		}

		const uint32 pagesX = static_cast<uint32>(meta.GetPageCountX());
		const uint32 pagesZ = static_cast<uint32>(meta.GetPageCountZ());
		const uint32 gridWidth = pagesX * VerticesPerPageEdge + 1;
		const uint32 gridHeight = pagesZ * VerticesPerPageEdge + 1;

		GrayImage16 zoneMap;
		if (!meta.zoneMap.empty())
		{
			if (!LoadGray16Png(meta.zoneMap, zoneMap))
			{
				return 1;
			}

			if (zoneMap.width != pagesX * TilesPerPage || zoneMap.height != pagesZ * TilesPerPage)
			{
				ELOG("Zone map is " << zoneMap.width << "x" << zoneMap.height << " but must hold one pixel per tile ("
					<< pagesX * TilesPerPage << "x" << pagesZ * TilesPerPage << ")");
				return 1;
			}
		}

		const std::set<std::pair<int32, int32>> pageFilter(meta.pages.begin(), meta.pages.end());

		// Validate all layer data before opening any destination page for writing.
		constexpr uint32 splatSide = terrain::constants::PixelsPerPage;
		RgbaImage8 splat;
		if (!args.splatmapPath.empty())
		{
			if (!LoadRgba8Png(args.splatmapPath, splat))
			{
				return 1;
			}
			if (splat.width != pagesX * (splatSide - 1) + 1 || splat.height != pagesZ * (splatSide - 1) + 1)
			{
				ELOG("Splat image dimensions must be (pagesX * 1008 + 1) by (pagesZ * 1008 + 1)!");
				return 1;
			}
			for (size_t i = 0; i < splat.pixels.size(); i += 4)
			{
				const uint32 sum = static_cast<uint32>(splat.pixels[i]) + splat.pixels[i + 1] + splat.pixels[i + 2] + splat.pixels[i + 3];
				if (sum != 255)
				{
					ELOG("Splat weights must sum to 255 at every pixel (invalid pixel " << i / 4 << ")!");
					return 1;
				}
			}
		}

		if (image.width != gridWidth || image.height != gridHeight)
		{
			WLOG("Heightmap is " << image.width << "x" << image.height << " but the lossless resolution for this page rect is "
				<< gridWidth << "x" << gridHeight << " - the image will be resampled bilinearly");
		}

		ILOG("Importing " << pagesX << "x" << pagesZ << " pages into world '" << meta.world << "' (height range "
			<< meta.minY << " to " << meta.maxY << ")...");

		// Build the continuous outer vertex grid. Sampling positions are computed globally,
		// so vertices shared between adjacent pages get bitwise-identical heights (no seams).
		std::vector<float> outerHeights(static_cast<size_t>(gridWidth) * gridHeight);
		for (uint32 z = 0; z < gridHeight; ++z)
		{
			const double v = static_cast<double>(z) / static_cast<double>(gridHeight - 1);
			for (uint32 x = 0; x < gridWidth; ++x)
			{
				const double u = static_cast<double>(x) / static_cast<double>(gridWidth - 1);
				outerHeights[static_cast<size_t>(z) * gridWidth + x] = SampleHeight(image, meta, u, v);
			}
		}

		if (meta.applyAsDelta)
		{
			return ApplyHeightDelta(meta, outerHeights, gridWidth, gridHeight, zoneMap, pageFilter);
		}

		// Full-precision outer normals (needed both for the outer grid itself and for
		// deriving inner normals the same way the engine does).
		std::vector<Vector3> outerNormals(static_cast<size_t>(gridWidth) * gridHeight);
		for (uint32 z = 0; z < gridHeight; ++z)
		{
			for (uint32 x = 0; x < gridWidth; ++x)
			{
				outerNormals[static_cast<size_t>(z) * gridWidth + x] = ComputeOuterNormal(outerHeights, gridWidth, gridHeight, x, z);
			}
		}

		uint32 savedPages = 0;
		uint32 keptPages = 0;
		uint32 zonedExistingTiles = 0;
		terrain_io::PageData page;
		for (int32 pageZ = meta.pageZ0; pageZ <= meta.pageZ1; ++pageZ)
		{
			for (int32 pageX = meta.pageX0; pageX <= meta.pageX1; ++pageX)
			{
				if (!pageFilter.empty() && !pageFilter.contains({ pageX, pageZ }))
				{
					continue;
				}

				const String filename = BuildPageFilename(meta.world, pageX, pageZ);
				if (meta.skipExistingPages && AssetRegistry::HasFile(filename))
				{
					uint32 changedTiles = 0;
					if (meta.fillExistingZones && !zoneMap.pixels.empty() && !FillZonesOfExistingPage(filename, zoneMap, meta, pageX, pageZ, changedTiles))
					{
						return 1;
					}

					zonedExistingTiles += changedTiles;
					++keptPages;
					continue;
				}

				page.Reset();

				if (!splat.pixels.empty())
				{
					const uint32 splatX = static_cast<uint32>(pageX - meta.pageX0) * (splatSide - 1);
					const uint32 splatZ = static_cast<uint32>(pageZ - meta.pageZ0) * (splatSide - 1);
					for (uint32 z = 0; z < splatSide; ++z)
					{
						for (uint32 x = 0; x < splatSide; ++x)
						{
							const size_t source = (static_cast<size_t>(splatZ + z) * splat.width + splatX + x) * 4;
							page.layers[static_cast<size_t>(z) * splatSide + x] =
								static_cast<uint32>(splat.pixels[source]) |
								(static_cast<uint32>(splat.pixels[source + 1]) << 8) |
								(static_cast<uint32>(splat.pixels[source + 2]) << 16) |
								(static_cast<uint32>(splat.pixels[source + 3]) << 24);
						}
					}
				}

				const uint32 baseX = static_cast<uint32>(pageX - meta.pageX0) * VerticesPerPageEdge;
				const uint32 baseZ = static_cast<uint32>(pageZ - meta.pageZ0) * VerticesPerPageEdge;

				// Outer grid slice
				for (uint32 z = 0; z < OuterSide; ++z)
				{
					for (uint32 x = 0; x < OuterSide; ++x)
					{
						const size_t gridIndex = static_cast<size_t>(baseZ + z) * gridWidth + (baseX + x);
						const size_t pageIndex = static_cast<size_t>(z) * OuterSide + x;
						page.heightmap[pageIndex] = outerHeights[gridIndex];

						const Vector3 &normal = outerNormals[gridIndex];
						page.normals[pageIndex] = EncodeNormalSNorm8(normal.x, normal.y, normal.z);
					}
				}

				// Inner vertices sit at the center of each outer grid quad. Heights are
				// sampled from the source image at those half-offset positions for real
				// extra detail; normals are averaged from the four surrounding outer
				// normals exactly like the engine derives them.
				for (uint32 j = 0; j < InnerSide; ++j)
				{
					const double v = (static_cast<double>(baseZ + j) + 0.5) / static_cast<double>(gridHeight - 1);
					for (uint32 i = 0; i < InnerSide; ++i)
					{
						const double u = (static_cast<double>(baseX + i) + 0.5) / static_cast<double>(gridWidth - 1);
						const size_t innerIndex = static_cast<size_t>(j) * InnerSide + i;
						page.innerHeightmap[innerIndex] = SampleHeight(image, meta, u, v);

						const size_t n00 = static_cast<size_t>(baseZ + j) * gridWidth + (baseX + i);
						const Vector3 average = ((outerNormals[n00] + outerNormals[n00 + 1] +
							outerNormals[n00 + gridWidth] + outerNormals[n00 + gridWidth + 1]) * 0.25f).NormalizedCopy();
						page.innerNormals[innerIndex] = EncodeNormalSNorm8(average.x, average.y, average.z);
					}
				}

				// Per-tile material assignment (empty = world default material)
				if (!meta.material.empty())
				{
					for (auto &materialName : page.materialNames)
					{
						materialName = meta.material;
					}
				}

				// Water quads: flag every 1/8-tile quad whose terrain dips below the water level
				if (meta.HasWaterLevel())
				{
					constexpr uint32 quadsPerTileSide = 8;
					bool pageHasWater = false;

					for (uint32 tileZ = 0; tileZ < terrain::constants::TilesPerPage; ++tileZ)
					{
						for (uint32 tileX = 0; tileX < terrain::constants::TilesPerPage; ++tileX)
						{
							uint64 mask = 0;
							for (uint32 quadZ = 0; quadZ < quadsPerTileSide; ++quadZ)
							{
								for (uint32 quadX = 0; quadX < quadsPerTileSide; ++quadX)
								{
									// Outer grid corners of this quad
									const uint32 vertX = tileX * quadsPerTileSide + quadX;
									const uint32 vertZ = tileZ * quadsPerTileSide + quadZ;
									const float corner00 = page.heightmap[vertZ * OuterSide + vertX];
									const float corner10 = page.heightmap[vertZ * OuterSide + vertX + 1];
									const float corner01 = page.heightmap[(vertZ + 1) * OuterSide + vertX];
									const float corner11 = page.heightmap[(vertZ + 1) * OuterSide + vertX + 1];
									const float lowest = std::min(std::min(corner00, corner10), std::min(corner01, corner11));
									if (lowest < meta.waterLevel)
									{
										mask |= uint64(1) << (quadX + quadZ * quadsPerTileSide);
									}
								}
							}

							if (mask != 0)
							{
								const uint32 tileIndex = tileX + tileZ * terrain::constants::TilesPerPage;
								page.waterQuadMasks[tileIndex] = mask;
								page.waterTypes[tileIndex] = meta.waterType;
								pageHasWater = true;
							}
						}
					}

					if (pageHasWater)
					{
						for (float &height : page.waterVertexHeights)
						{
							height = meta.waterLevel;
						}
						page.waterMaterialName = meta.waterMaterial;
					}
				}

				if (!zoneMap.pixels.empty())
				{
					ApplyZones(zoneMap, meta, pageX, pageZ, page.zones, false);
				}

				if (!WritePage(filename, page))
				{
					return 1;
				}

				++savedPages;
			}
		}

		ILOG("Successfully wrote " << savedPages << " terrain pages");
		if (keptPages > 0)
		{
			ILOG("Kept " << keptPages << " existing pages" << (meta.fillExistingZones ? " (zone ids filled in on " + std::to_string(zonedExistingTiles) + " of their unzoned tiles)" : String()));
		}
		ILOG("Note: if the world is currently open in the editor, close and reopen it to see the imported terrain");
		return 0;
	}
}
