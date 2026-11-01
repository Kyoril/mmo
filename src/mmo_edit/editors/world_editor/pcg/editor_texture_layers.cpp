// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "editor_texture_layers.h"

#include "section_bake_job.h"

#include "math/vector3.h"
#include "pcg/section_asset.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

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
			if (m_terrain.GetPageIndexFromGlobalTileIndex(tileX, tileZ, pageX, pageZ) &&
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

	pcg::graph::TextureLayerLookup EditorTextureLayers::MakeLookup()
	{
		return [this](const float x, const float z, const String& textureKey)
		{
			return FindLayer(x, z, textureKey);
		};
	}
}
