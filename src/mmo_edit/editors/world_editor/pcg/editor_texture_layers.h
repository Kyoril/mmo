// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "pcg/graph/terrain_compose.h"

#include <array>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		class Terrain;
	}

	/// @brief Finds the paint layer of the terrain tile under a world position that carries a texture, the way
	///        section paint is matched: the texture key of each tile layer (GetTerrainLayerTextureKey: layer binding,
	///        Albedo0N fallback) compared with pcg::TextureKeysMatch, first matching layer wins.
	/// @details Caches the four layer textures per tile (raw and normalised) and the normalised form of every
	///          queried key for its lifetime, so it must not outlive a change of tile materials it should see (one
	///          build). Main thread only (the terrain is not thread safe, the cache is not synchronised), except for
	///          copies made by CopyWarmCache, which never read the terrain.
	class EditorTextureLayers final : public NonCopyable
	{
	public:
		/// @brief Creates the adapter.
		/// @param terrain The terrain to read. Must outlive the adapter.
		explicit EditorTextureLayers(terrain::Terrain& terrain);

	public:
		/// @brief Global index of the tile under a world position.
		/// @return False outside the terrain.
		bool GetTileIndex(float x, float z, int32& tileX, int32& tileZ) const;

		/// @brief Texture keys (as GetTerrainLayerTextureKey returns them) of the four paint layers of a tile. All
		///        empty if the tile's page is not loaded.
		const std::array<String, 4>& GetTileTextures(int32 tileX, int32 tileZ);

		/// @brief Layer (0..3) of the tile whose texture matches textureKey; -1 if none does or textureKey is empty.
		int8 FindLayer(int32 tileX, int32 tileZ, const String& textureKey);

		/// @brief FindLayer for the tile under a world position; -1 outside the terrain.
		int8 FindLayer(float x, float z, const String& textureKey);

		/// @brief Reads the layer textures of every tile overlapping rect (grown by one tile) into the cache.
		void Prewarm(const pcg::WorldRect& rect);

		/// @brief A copy of the cache that never reads the terrain: a tile it has not cached has no layer textures. It
		///        may be used on another thread (one copy per thread) while nothing changes tile materials. Must not
		///        outlive this adapter's terrain.
		[[nodiscard]] std::unique_ptr<EditorTextureLayers> CopyWarmCache() const;

		/// @brief A lookup for pcg::graph::PreparedTerrain::Evaluate that calls FindLayer on this adapter, which must
		///        outlive the returned function.
		[[nodiscard]] pcg::graph::TextureLayerLookup MakeLookup();

	private:
		/// The layer texture keys of one tile, as read and normalised (pcg::NormalizeTextureKey).
		struct TileTextures
		{
			std::array<String, 4> keys;
			std::array<String, 4> normalized;
		};

		/// The cached textures of a tile, read on first use.
		const TileTextures& GetTile(int32 tileX, int32 tileZ);

		/// The normalised form of a queried texture key, cached (callers query a handful of keys many times).
		const String& NormalizeQuery(const String& textureKey);

	private:
		terrain::Terrain& m_terrain;

		/// Layer textures by global tile index (map nodes are stable, so m_lastTile stays valid).
		std::map<std::pair<int32, int32>, TileTextures> m_tiles;

		/// The tile of the last lookup (consecutive lookups mostly hit the same tile).
		int32 m_lastTileX = 0;
		int32 m_lastTileZ = 0;
		const TileTextures* m_lastTile = nullptr;

		/// Queried keys and their normalised form.
		std::vector<std::pair<String, String>> m_queries;

		/// A CopyWarmCache copy: a tile missing from m_tiles is not read from the terrain.
		bool m_cacheOnly = false;
	};
}
