// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "pcg/graph/evaluator.h"
#include "pcg/terrain_contribution.h"

#include <array>
#include <functional>
#include <map>
#include <utility>

namespace mmo
{
	namespace terrain
	{
		class Terrain;
	}

	/// @brief Answers the PCG graph evaluator's terrain queries from the editor's terrain::Terrain.
	/// @details Reads only; main thread only (the terrain is not thread safe). Pages that are not
	///          loaded have no terrain: callers load the region first (IWorldEditor::EnsurePagesLoaded). A rebuild
	///          job uses one on its worker thread while nothing writes the terrain: PrewarmTiles reads the tile
	///          materials first and SetCacheOnly keeps the worker from reading them.
	class EditorTerrainSampler final : public pcg::graph::ITerrainSampler
	{
	public:
		/// @brief Creates the sampler.
		/// @param terrain The terrain to read. Must outlive the sampler.
		/// @param revision Read on every Revision() call (the world's terrain revision,
		///        IWorldEditor::GetTerrainRevision).
		EditorTerrainSampler(terrain::Terrain& terrain, std::function<uint64()> revision);

	public:
		/// @brief Smooth terrain height (Terrain::TryGetSmoothHeightAt). False where no page is loaded.
		bool Height(float x, float z, float& h) const override;

		/// @brief Smooth terrain normal (Terrain::GetSmoothNormalAt). +Y outside the terrain.
		Vector3 Normal(float x, float z) const override;

		/// @brief Blend weight 0..1 of the paint layer(s) of the tile under the position whose albedo texture
		///        key matches textureKey, with the section paint matching rule (GetTerrainLayerTextureKey:
		///        layer binding, Albedo0N fallback; compared with pcg::TextureKeysMatch). Several matching
		///        layers add up (clamped to 1). 0 if no layer matches or no page is loaded there.
		float SplatWeight(const String& textureKey, float x, float z) const override;

		/// @brief The paint layers of the tile under the position that SplatWeight sums for textureKey: bit i set =
		///        layer i matches. 0 for an empty key, outside the terrain or where no page is loaded.
		uint8 GetMatchingLayers(const String& textureKey, float x, float z) const;

		/// @brief Water surface height (Terrain::GetWaterHeightAtWorldPos). False where there is no water
		///        (Terrain::HasWaterAtWorldPos).
		bool WaterLevel(float x, float z, float& level) const override;

		/// @brief The value of the revision callback (0 without a callback).
		uint64 Revision() const override;

		/// @brief Caches the texture keys of every tile overlapping the rect (grown by one tile) whose page is loaded.
		void PrewarmTiles(const pcg::WorldRect& rect);

		/// @brief While set, a tile missing from the cache has no texture keys instead of being read (no material is
		///        touched).
		void SetCacheOnly(bool cacheOnly) { m_cacheOnly = cacheOnly; }

	private:
		/// @brief Normalised texture keys (pcg::NormalizeTextureKey) of the four paint layers of a tile,
		///        cached per tile until the revision changes. All empty if the tile has no loaded page.
		const std::array<String, 4>& GetTileTextureKeys(int32 tileX, int32 tileZ) const;

	private:
		terrain::Terrain& m_terrain;
		std::function<uint64()> m_revision;

		/// Tile texture keys by global tile index, valid for m_cacheRevision.
		mutable std::map<std::pair<int32, int32>, std::array<String, 4>> m_tileTextureKeys;
		mutable uint64 m_cacheRevision = 0;
		/// See SetCacheOnly.
		bool m_cacheOnly = false;
	};
}
