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

	const std::array<String, 4>& EditorTextureLayers::GetTileTextures(const int32 tileX, const int32 tileZ)
	{
		const auto key = std::make_pair(tileX, tileZ);
		const auto cached = m_tileTextures.find(key);
		if (cached != m_tileTextures.end())
		{
			return cached->second;
		}

		std::array<String, 4> textures;
		int32 pageX = 0, pageZ = 0, localX = 0, localZ = 0;
		if (m_terrain.GetPageIndexFromGlobalTileIndex(tileX, tileZ, pageX, pageZ) &&
			m_terrain.GetLocalTileIndexByGlobalTileIndex(tileX, tileZ, localX, localZ))
		{
			if (const terrain::Page* page = m_terrain.GetPage(static_cast<uint32>(pageX), static_cast<uint32>(pageZ)))
			{
				const MaterialPtr material = page->GetTileMaterial(static_cast<uint32>(localX), static_cast<uint32>(localZ));
				for (uint8 layer = 0; layer < 4; ++layer)
				{
					textures[layer] = GetTerrainLayerTextureKey(material, layer);
				}
			}
		}

		return m_tileTextures.emplace(key, std::move(textures)).first->second;
	}

	int8 EditorTextureLayers::FindLayer(const int32 tileX, const int32 tileZ, const String& textureKey)
	{
		if (textureKey.empty())
		{
			return -1;
		}

		const std::array<String, 4>& textures = GetTileTextures(tileX, tileZ);
		for (size_t layer = 0; layer < textures.size(); ++layer)
		{
			if (pcg::TextureKeysMatch(textures[layer], textureKey))
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
