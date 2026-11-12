// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "base_terrain_sampler.h"

#include "terrain/constants.h"

namespace mmo
{
	BaseTerrainSampler::BaseTerrainSampler(const EditorTerrainSampler& live, terrain::Terrain& terrain, const pcg::TerrainContribution& contribution)
		: m_access(terrain)
		, m_view(live, m_access, contribution,
			[&live](const String& textureKey, const float x, const float z)
			{
				return live.GetMatchingLayers(textureKey, x, z);
			},
			static_cast<int32>(terrain::constants::OuterVerticesPerPageSide - 1))
	{
	}
}
