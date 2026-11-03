// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "editor_texture_layers.h"

#include "section_bake_job.h"

#include "math/vector3.h"
#include "pcg/section_asset.h"
#include "terrain/constants.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	EditorTextureLayers::EditorTextureLayers(terrain::Terrain& terrain)
		: m_terrain(terrain)
	{
	}

	bool EditorTextureLayers::GetTileIndex(const float x, const float z, int32& tileX, int32& tileZ) const
	{
		return m_terrain.GetTileIndexByWorldPosition(Vector3(x, 0.0f, z), tileX, tileZ);
	}

	const EditorTextureLayers::TileTextures& EditorTextureLayers::GetTile(const int32 tileX, const int32 tileZ)
	{
		if (m_lastTile != nullptr && m_lastTileX == tileX && m_lastTileZ == tileZ)
		{
			return *m_lastTile;
		}

		const auto key = std::make_pair(tileX, tileZ);
		auto cached = m_tiles.find(key);
		if (cached == m_tiles.end())
		{
			TileTextures textures;
			int32 pageX = 0, pageZ = 0, localX = 0, localZ = 0;
			if (!m_cacheOnly && m_terrain.GetPageIndexFromGlobalTileIndex(tileX, tileZ, pageX, pageZ) &&
				m_terrain.GetLocalTileIndexByGlobalTileIndex(tileX, tileZ, localX, localZ))
			{
				if (const terrain::Page* page = m_terrain.GetPage(static_cast<uint32>(pageX), static_cast<uint32>(pageZ)))
				{
					const MaterialPtr material = page->GetTileMaterial(static_cast<uint32>(localX), static_cast<uint32>(localZ));
					for (uint8 layer = 0; layer < 4; ++layer)
					{
						textures.keys[layer] = GetTerrainLayerTextureKey(material, layer);
						textures.normalized[layer] = pcg::NormalizeTextureKey(textures.keys[layer]);
					}
				}
			}
			cached = m_tiles.emplace(key, std::move(textures)).first;
		}

		m_lastTileX = tileX;
		m_lastTileZ = tileZ;
		m_lastTile = &cached->second;
		return cached->second;
	}

	const String& EditorTextureLayers::NormalizeQuery(const String& textureKey)
	{
		for (const auto& query : m_queries)
		{
			if (query.first == textureKey)
			{
				return query.second;
			}
		}
		m_queries.emplace_back(textureKey, pcg::NormalizeTextureKey(textureKey));
		return m_queries.back().second;
	}

	const std::array<String, 4>& EditorTextureLayers::GetTileTextures(const int32 tileX, const int32 tileZ)
	{
		return GetTile(tileX, tileZ).keys;
	}

	int8 EditorTextureLayers::FindLayer(const int32 tileX, const int32 tileZ, const String& textureKey)
	{
		if (textureKey.empty())
		{
			return -1;
		}

		// Equal normalised keys is what pcg::TextureKeysMatch tests (normalising keeps the length).
		const String& wanted = NormalizeQuery(textureKey);
		const TileTextures& tile = GetTile(tileX, tileZ);
		for (size_t layer = 0; layer < tile.normalized.size(); ++layer)
		{
			if (tile.normalized[layer] == wanted)
			{
				return static_cast<int8>(layer);
			}
		}
		return -1;
	}

	int8 EditorTextureLayers::FindLayer(const float x, const float z, const String& textureKey)
	{
		int32 tileX = 0, tileZ = 0;
		if (!GetTileIndex(x, z, tileX, tileZ))
		{
			return -1;
		}
		return FindLayer(tileX, tileZ, textureKey);
	}

	void EditorTextureLayers::Prewarm(const pcg::WorldRect& rect)
	{
		int32 minX = 0, minZ = 0, maxX = 0, maxZ = 0;
		if (!GetTileIndex(rect.minX, rect.minZ, minX, minZ) || !GetTileIndex(rect.maxX, rect.maxZ, maxX, maxZ))
		{
			// A corner off the terrain: clamp through the corners that are on it, tile by tile.
			const int32 tiles = static_cast<int32>(m_terrain.GetWidth() * terrain::constants::TilesPerPage);
			const int32 rows = static_cast<int32>(m_terrain.GetHeight() * terrain::constants::TilesPerPage);
			const float half = m_terrain.GetWidth() * terrain::constants::PageSize * 0.5f;
			const float halfZ = m_terrain.GetHeight() * terrain::constants::PageSize * 0.5f;
			minX = std::clamp(static_cast<int32>(std::floor((rect.minX + half) / terrain::constants::TileSize)), 0, tiles - 1);
			maxX = std::clamp(static_cast<int32>(std::floor((rect.maxX + half) / terrain::constants::TileSize)), 0, tiles - 1);
			minZ = std::clamp(static_cast<int32>(std::floor((rect.minZ + halfZ) / terrain::constants::TileSize)), 0, rows - 1);
			maxZ = std::clamp(static_cast<int32>(std::floor((rect.maxZ + halfZ) / terrain::constants::TileSize)), 0, rows - 1);
		}

		for (int32 z = minZ - 1; z <= maxZ + 1; ++z)
		{
			for (int32 x = minX - 1; x <= maxX + 1; ++x)
			{
				GetTile(x, z);
			}
		}
	}

	std::unique_ptr<EditorTextureLayers> EditorTextureLayers::CopyWarmCache() const
	{
		auto copy = std::make_unique<EditorTextureLayers>(m_terrain);
		copy->m_tiles = m_tiles;
		copy->m_queries = m_queries;
		copy->m_cacheOnly = true;
		return copy;
	}

	pcg::graph::TextureLayerLookup EditorTextureLayers::MakeLookup()
	{
		return [this](const float x, const float z, const String& textureKey)
		{
			return FindLayer(x, z, textureKey);
		};
	}
}
