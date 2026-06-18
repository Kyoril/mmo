// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "server_water_map.h"

#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"
#include "terrain/constants.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	using namespace terrain;

	namespace
	{
		// Water quad chunk magic — must match terrain::constants::WaterQuadChunk ('QWCM').
		const uint32 s_waterQuadMagic = *MakeChunkMagic('QWCM');

		// Terrain is always a 64x64 page grid with the world origin centered at page (32, 32).
		constexpr int32 PageGridSize = static_cast<int32>(constants::MaxPages);
		constexpr int32 PageCenter = PageGridSize / 2;

		constexpr uint32 OuterVertsPerPage = constants::OuterVerticesPerPageSide; // 129
	}

	ServerWaterMap::ServerWaterMap(const std::string& mapName)
	{
		uint32 pagesWithWater = 0;
		for (uint32 z = 0; z < constants::MaxPages; ++z)
		{
			for (uint32 x = 0; x < constants::MaxPages; ++x)
			{
				if (LoadPage(x, z, mapName))
				{
					++pagesWithWater;
				}
			}
		}

		if (pagesWithWater == 0)
		{
			WLOG("ServerWaterMap: no water data found for map '" << mapName
				<< "' — swim validation will reject all start-swim attempts on this map");
		}
		else
		{
			DLOG("ServerWaterMap: loaded water data from " << pagesWithWater
				<< " page(s) for map '" << mapName << "'");
		}
	}

	bool ServerWaterMap::LoadPage(const uint32 pageX, const uint32 pageZ, const std::string& mapName)
	{
		// Runtime page-file naming convention (see terrain::Page::GetPageFilename).
		const std::string pageFile = "Worlds/" + mapName + "/" + mapName + "/Terrain/"
			+ std::to_string(pageX) + "_" + std::to_string(pageZ) + ".tile";

		if (!AssetRegistry::HasFile(pageFile))
		{
			return false;
		}

		auto file = AssetRegistry::OpenFile(pageFile);
		if (!file)
		{
			return false;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		while (reader)
		{
			uint32 chunkId = 0, chunkSize = 0;
			if (!(reader >> io::read<uint32>(chunkId) >> io::read<uint32>(chunkSize)))
			{
				break;
			}

			if (chunkId != s_waterQuadMagic)
			{
				reader >> io::skip(chunkSize);
				continue;
			}

			// Parse the water quad chunk (layout written by terrain::Page::Save):
			//   uint16 count
			//   count × { uint16 tileIdx, uint8 waterType, uint64 quadMask }
			//   OuterVerticesPerPageSide^2 × float (water surface vertex heights)
			//   dynamic_range<uint16> material name (ignored)
			PageWater pageWater;

			uint16 count = 0;
			if (!(reader >> io::read<uint16>(count)))
			{
				return false;
			}

			bool anyWater = false;
			for (uint16 i = 0; i < count; ++i)
			{
				uint16 tileIdx = 0;
				uint8 waterType = 0;
				uint64 mask = 0;
				if (!(reader >> io::read<uint16>(tileIdx) >> io::read<uint8>(waterType) >> io::read<uint64>(mask)))
				{
					return false;
				}

				if (mask != 0)
				{
					pageWater.quadMasks[tileIdx] = mask;
					anyWater = true;
				}
			}

			pageWater.vertexHeights.resize(static_cast<size_t>(OuterVertsPerPage) * OuterVertsPerPage, 0.0f);
			for (auto& h : pageWater.vertexHeights)
			{
				if (!(reader >> io::read<float>(h)))
				{
					return false;
				}
			}

			// Material name and any remaining bytes of this chunk are not needed server-side.
			if (!anyWater)
			{
				return false;
			}

			m_pages.emplace(pageX + pageZ * constants::MaxPages, std::move(pageWater));
			return true;
		}

		return false;
	}

	const ServerWaterMap::PageWater* ServerWaterMap::GetPage(const uint32 pageX, const uint32 pageZ) const
	{
		const auto it = m_pages.find(pageX + pageZ * constants::MaxPages);
		return (it != m_pages.end()) ? &it->second : nullptr;
	}

	bool ServerWaterMap::HasWater(const float x, const float z) const
	{
		const float halfW = static_cast<float>(PageGridSize * constants::PageSize) * 0.5f;
		const int32 pageX = static_cast<int32>(std::floor((x + halfW) / constants::PageSize));
		const int32 pageZ = static_cast<int32>(std::floor((z + halfW) / constants::PageSize));
		if (pageX < 0 || pageZ < 0 || pageX >= PageGridSize || pageZ >= PageGridSize)
		{
			return false;
		}

		const PageWater* page = GetPage(static_cast<uint32>(pageX), static_cast<uint32>(pageZ));
		if (!page)
		{
			return false;
		}

		const float pageOriginX = static_cast<float>((pageX - PageCenter) * constants::PageSize);
		const float pageOriginZ = static_cast<float>((pageZ - PageCenter) * constants::PageSize);
		const float tileSizeF = static_cast<float>(constants::TileSize);
		const float quadSizeF = tileSizeF / 8.0f;

		const auto tileX = static_cast<uint32>((x - pageOriginX) / tileSizeF);
		const auto tileZ = static_cast<uint32>((z - pageOriginZ) / tileSizeF);
		if (tileX >= constants::TilesPerPage || tileZ >= constants::TilesPerPage)
		{
			return false;
		}

		const float tileLocalX = (x - pageOriginX) - tileX * tileSizeF;
		const float tileLocalZ = (z - pageOriginZ) - tileZ * tileSizeF;
		const uint32 qx = std::min(static_cast<uint32>(tileLocalX / quadSizeF), 7u);
		const uint32 qz = std::min(static_cast<uint32>(tileLocalZ / quadSizeF), 7u);

		const auto maskIt = page->quadMasks.find(static_cast<uint16>(tileX + tileZ * constants::TilesPerPage));
		if (maskIt == page->quadMasks.end())
		{
			return false;
		}

		return (maskIt->second & (1ULL << (qx + qz * 8))) != 0;
	}

	bool ServerWaterMap::GetWaterSurface(const float x, const float z, float& outSurfaceY) const
	{
		if (!HasWater(x, z))
		{
			return false;
		}

		const float halfW = static_cast<float>(PageGridSize * constants::PageSize) * 0.5f;
		const int32 pageX = static_cast<int32>(std::floor((x + halfW) / constants::PageSize));
		const int32 pageZ = static_cast<int32>(std::floor((z + halfW) / constants::PageSize));

		const PageWater* page = GetPage(static_cast<uint32>(pageX), static_cast<uint32>(pageZ));
		if (!page || page->vertexHeights.empty())
		{
			return false;
		}

		const float pageOriginX = static_cast<float>((pageX - PageCenter) * constants::PageSize);
		const float pageOriginZ = static_cast<float>((pageZ - PageCenter) * constants::PageSize);
		const float spacing = static_cast<float>(constants::TileSize) / 8.0f;

		// Bilinear interpolation over the page's water vertex height grid.
		const float localX = (x - pageOriginX) / spacing;
		const float localZ = (z - pageOriginZ) / spacing;
		const int pvx0 = std::max(0, std::min(static_cast<int>(localX), static_cast<int>(OuterVertsPerPage) - 2));
		const int pvz0 = std::max(0, std::min(static_cast<int>(localZ), static_cast<int>(OuterVertsPerPage) - 2));

		const float fx = localX - pvx0;
		const float fz = localZ - pvz0;

		auto vh = [&](const int vx, const int vz) -> float
		{
			return page->vertexHeights[static_cast<size_t>(vx) + static_cast<size_t>(vz) * OuterVertsPerPage];
		};

		const float h00 = vh(pvx0, pvz0);
		const float h10 = vh(pvx0 + 1, pvz0);
		const float h01 = vh(pvx0, pvz0 + 1);
		const float h11 = vh(pvx0 + 1, pvz0 + 1);

		outSurfaceY = h00 * (1.0f - fx) * (1.0f - fz)
			+ h10 * fx * (1.0f - fz)
			+ h01 * (1.0f - fx) * fz
			+ h11 * fx * fz;
		return true;
	}
}
