// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_pages.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"
#include "terrain_io/page_io.h"

namespace mmo
{
	namespace
	{
		constexpr uint32 VerticesPerPageEdge = terrain::constants::OuterVerticesPerPageSide - 1; // 128
	}

	String BuildPageFilename(const String &world, const int32 pageX, const int32 pageZ)
	{
		return "Worlds/" + world + "/" + world + "/Terrain/" + std::to_string(pageX) + "_" + std::to_string(pageZ) + ".tile";
	}

	String BuildWorldFilename(const String &world)
	{
		return "Worlds/" + world + "/" + world + ".hwld";
	}

	bool LoadZoneGrid(const String &world, const int32 pageX0, const int32 pageZ0, const int32 pageX1, const int32 pageZ1, ZoneGrid &out)
	{
		out.pageX0 = pageX0;
		out.pageZ0 = pageZ0;
		out.pageX1 = pageX1;
		out.pageZ1 = pageZ1;
		out.width = static_cast<uint32>(pageX1 - pageX0 + 1) * VerticesPerPageEdge + 1;
		out.height = static_cast<uint32>(pageZ1 - pageZ0 + 1) * VerticesPerPageEdge + 1;
		out.heights.assign(static_cast<size_t>(out.width) * out.height, 0.0f);
		out.normals.assign(static_cast<size_t>(out.width) * out.height, EncodedNormal8{ 0, 127, 0 });
		out.loadedPageCount = 0;

		for (int32 pageZ = pageZ0; pageZ <= pageZ1; ++pageZ)
		{
			for (int32 pageX = pageX0; pageX <= pageX1; ++pageX)
			{
				const String filename = BuildPageFilename(world, pageX, pageZ);
				const auto file = AssetRegistry::OpenFile(filename);
				if (!file)
				{
					WLOG("Terrain page '" << filename << "' does not exist, treating as blank page");
					continue;
				}

				io::StreamSource source{ *file };
				io::Reader reader{ source };

				terrain_io::PageData page;
				if (!terrain_io::LoadPage(reader, page))
				{
					ELOG("Failed to load terrain page '" << filename << "'!");
					continue;
				}

				const uint32 baseX = static_cast<uint32>(pageX - pageX0) * VerticesPerPageEdge;
				const uint32 baseZ = static_cast<uint32>(pageZ - pageZ0) * VerticesPerPageEdge;

				for (uint32 z = 0; z < terrain::constants::OuterVerticesPerPageSide; ++z)
				{
					for (uint32 x = 0; x < terrain::constants::OuterVerticesPerPageSide; ++x)
					{
						const size_t gridIndex = static_cast<size_t>(baseZ + z) * out.width + (baseX + x);
						const size_t pageIndex = static_cast<size_t>(z) * terrain::constants::OuterVerticesPerPageSide + x;
						out.heights[gridIndex] = page.heightmap[pageIndex];
						out.normals[gridIndex] = page.normals[pageIndex];
					}
				}

				++out.loadedPageCount;
			}
		}

		return out.loadedPageCount > 0;
	}

	Vector3 ComputeOuterNormal(const std::vector<float> &heights, const uint32 width, const uint32 height, const uint32 x, const uint32 z)
	{
		// Replicates terrain::Page::CalculateNormalAt exactly, including the engine's
		// scaling constant of PageSize / 129 (not the true vertex spacing of PageSize / 128).
		const float scaling = static_cast<float>(terrain::constants::PageSize / static_cast<double>(terrain::constants::OuterVerticesPerPageSide));

		const float heightCenter = heights[static_cast<size_t>(z) * width + x];
		const float heightLeft = (x > 0) ? heights[static_cast<size_t>(z) * width + (x - 1)] : heightCenter;
		const float heightRight = (x < width - 1) ? heights[static_cast<size_t>(z) * width + (x + 1)] : heightCenter;
		const float heightUp = (z > 0) ? heights[static_cast<size_t>(z - 1) * width + x] : heightCenter;
		const float heightDown = (z < height - 1) ? heights[static_cast<size_t>(z + 1) * width + x] : heightCenter;

		const float dx = (heightRight - heightLeft) / (2.0f * scaling);
		const float dz = (heightDown - heightUp) / (2.0f * scaling);

		Vector3 normal(-dx, 1.0f, -dz);
		normal.Normalize();
		return normal;
	}
}
