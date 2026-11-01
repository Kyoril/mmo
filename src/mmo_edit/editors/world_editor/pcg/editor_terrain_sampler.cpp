// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "editor_terrain_sampler.h"

#include "section_bake_job.h"

#include "pcg/section_asset.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

#include <algorithm>

namespace mmo
{
	EditorTerrainSampler::EditorTerrainSampler(terrain::Terrain& terrain, std::function<uint64()> revision)
		: m_terrain(terrain)
		, m_revision(std::move(revision))
	{
	}

	bool EditorTerrainSampler::Height(const float x, const float z, float& h) const
	{
		return m_terrain.TryGetSmoothHeightAt(x, z, h);
	}

	Vector3 EditorTerrainSampler::Normal(const float x, const float z) const
	{
		return m_terrain.GetSmoothNormalAt(x, z);
	}

	uint8 EditorTerrainSampler::GetMatchingLayers(const String& textureKey, const float x, const float z) const
	{
		if (textureKey.empty())
		{
			return 0;
		}

		int32 tileX = 0, tileZ = 0;
		if (!m_terrain.GetTileIndexByWorldPosition(Vector3(x, 0.0f, z), tileX, tileZ))
		{
			return 0;
		}

		const String key = pcg::NormalizeTextureKey(textureKey);
		const std::array<String, 4>& tileKeys = GetTileTextureKeys(tileX, tileZ);

		uint8 mask = 0;
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			if (!tileKeys[layer].empty() && tileKeys[layer] == key)
			{
				mask |= static_cast<uint8>(1u << layer);
			}
		}
		return mask;
	}

	float EditorTerrainSampler::SplatWeight(const String& textureKey, const float x, const float z) const
	{
		const uint8 mask = GetMatchingLayers(textureKey, x, z);
		float weight = 0.0f;
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			if ((mask & (1u << layer)) != 0)
			{
				weight += m_terrain.GetLayerValueAt(x, z, layer);
			}
		}

		return std::min(weight, 1.0f);
	}

	bool EditorTerrainSampler::WaterLevel(const float x, const float z, float& level) const
	{
		if (!m_terrain.HasWaterAtWorldPos(x, z))
		{
			return false;
		}

		level = m_terrain.GetWaterHeightAtWorldPos(x, z);
		return true;
	}

	uint64 EditorTerrainSampler::Revision() const
	{
		return m_revision ? m_revision() : 0;
	}

	const std::array<String, 4>& EditorTerrainSampler::GetTileTextureKeys(const int32 tileX, const int32 tileZ) const
	{
		// Tile materials are terrain data, so a material change bumps the revision and drops the cache.
		const uint64 revision = Revision();
		if (revision != m_cacheRevision)
		{
			m_tileTextureKeys.clear();
			m_cacheRevision = revision;
		}

		const auto key = std::make_pair(tileX, tileZ);
		const auto cached = m_tileTextureKeys.find(key);
		if (cached != m_tileTextureKeys.end())
		{
			return cached->second;
		}

		std::array<String, 4> keys;
		int32 pageX = 0, pageZ = 0, localX = 0, localZ = 0;
		const terrain::Page* page = nullptr;
		if (m_terrain.GetPageIndexFromGlobalTileIndex(tileX, tileZ, pageX, pageZ) &&
			m_terrain.GetLocalTileIndexByGlobalTileIndex(tileX, tileZ, localX, localZ))
		{
			page = m_terrain.GetPage(static_cast<uint32>(pageX), static_cast<uint32>(pageZ));
		}

		// A page that is not loaded yet is not cached: it may stream in without a revision change.
		if (page == nullptr || !page->IsPrepared())
		{
			static const std::array<String, 4> none;
			return none;
		}

		const MaterialPtr material = page->GetTileMaterial(static_cast<uint32>(localX), static_cast<uint32>(localZ));
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			keys[layer] = pcg::NormalizeTextureKey(GetTerrainLayerTextureKey(material, layer));
		}

		return m_tileTextureKeys.emplace(key, std::move(keys)).first->second;
	}
}
