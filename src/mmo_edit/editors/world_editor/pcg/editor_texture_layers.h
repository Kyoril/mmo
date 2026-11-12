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
	///          copies made by CopyWarmCache, which never read the terrain or load materials. Tiles a build switches to
	///          another material answer with that material's layers (SetTileMaterialOverrides).
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

		/// @brief Texture keys (as GetTerrainLayerTextureKey returns them) of the four paint layers of a tile, those of
		///        its override material if SetTileMaterialOverrides named one. All empty if the tile's page is not loaded.
		const std::array<String, 4>& GetTileTextures(int32 tileX, int32 tileZ);

		/// @brief Layer (0..3) of the tile whose texture matches textureKey; -1 if none does or textureKey is empty.
		int8 FindLayer(int32 tileX, int32 tileZ, const String& textureKey);

		/// @brief FindLayer for the tile under a world position; -1 outside the terrain.
		int8 FindLayer(float x, float z, const String& textureKey);

		/// @brief Reads the layer textures of every tile overlapping rect (grown by one tile) into the cache.
		void Prewarm(const pcg::WorldRect& rect);

		/// @brief Reads the layer textures of a material into the cache, if not cached yet (main thread; does nothing on a
		///        CopyWarmCache copy). "" is the terrain's default material, as EditorTerrainAccess names it. A material
		///        that cannot be loaded has no layer textures.
		void LoadMaterialLayers(const String& material);

		/// @brief Whether the layer textures of a material are cached (LoadMaterialLayers ran for it here, or in the
		///        adapter this one was copied from before the copy).
		[[nodiscard]] bool HasMaterialLayers(const String& material) const;

		/// @brief The normalised (pcg::NormalizeTextureKey) texture keys of a material's four layers; null if the
		///        material is not cached (see LoadMaterialLayers).
		[[nodiscard]] const std::array<String, 4>* GetNormalizedMaterialLayers(const String& material) const;

		/// @brief Makes the given tiles answer with the layers of the named material instead of the tile's own, replacing
		///        earlier overrides (pcg::graph::PreparedTerrain::GetTileMaterials). A material that is not cached is read
		///        first (LoadMaterialLayers), except on a CopyWarmCache copy, where such a tile has no layer textures:
		///        load every named material on the adapter the copies are made from before making them.
		void SetTileMaterialOverrides(const std::vector<pcg::graph::TileMaterialRequest>& overrides);

		/// @brief A copy of the cache that never reads the terrain: a tile it has not cached has no layer textures. It
		///        keeps the cached materials and the overrides. It may be used on another thread (one copy per thread)
		///        while nothing changes tile materials. Must not outlive this adapter's terrain.
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

		/// The cached textures of a tile (its override material's, if it has one), read on first use.
		const TileTextures& GetTile(int32 tileX, int32 tileZ);

		/// Loads a material ("" = the terrain's default material) and reads its layer textures (main thread).
		TileTextures ReadMaterial(const String& material) const;

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

		/// Layer textures by material name ("" = the terrain's default material).
		std::map<String, TileTextures> m_materials;

		/// The overrides as given, and the cached material each overridden tile answers with (map nodes are stable;
		/// null for a material that is not cached).
		std::vector<pcg::graph::TileMaterialRequest> m_overrideRequests;
		std::map<std::pair<int32, int32>, const TileTextures*> m_overrides;

		/// A CopyWarmCache copy: a tile missing from m_tiles is not read from the terrain.
		bool m_cacheOnly = false;
	};
}
