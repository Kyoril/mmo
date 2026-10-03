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
