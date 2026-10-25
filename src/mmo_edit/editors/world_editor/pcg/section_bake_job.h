// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/material.h"
#include "pcg/section_bake.h"

#include <array>

namespace mmo
{
	class WorldEditorInstance;
	class MapEntity;

	/// @brief Asset folder holding every section world (editor-only, not shipped).
	extern const char* const SectionWorldFolder;

	/// @brief Terrain material of a new section world unless another one is chosen.
	extern const char* const DefaultSectionTerrainMaterial;

	/// @brief Packed splat value of an untouched section world: layer 0 at full weight. Every page of a
	///        new section world is filled with it, and the bake treats it as "not painted".
	constexpr uint32 SectionDefaultPaint = 0x000000FFu;

	/// @brief Spacing along x between the variants of a new section world, in metres.
	constexpr float SectionVariantSpacing = 60.0f;

	/// @brief Asset path of a section world: Editor/Pcg/Sections/<name>/<name>.hwld.
	[[nodiscard]] String GetSectionWorldPath(const String& name);

	/// @brief Asset path of a section world in another folder: <folder>/<name>/<name>.hwld (the baked
	///        asset then goes to <folder>/<name>.hsec). An empty folder means SectionWorldFolder.
	[[nodiscard]] String GetSectionWorldPath(const String& folder, const String& name);

	/// @brief Checks a folder for section worlds: SectionWorldFolder (Editor/Pcg/Sections) or
	///        Editor/Pcg/Biomes/<Biome>/Sections with a biome name allowed by IsValidSectionName (the folders
	///        ListSectionAssets searches; never Editor/Pcg/Worlds, where the world records live).
	/// @param error Receives the reason if the folder is not valid.
	bool IsValidSectionFolder(const String& folder, String& error);

	/// @brief Name of a section: the stem of its world file (the .hsecdef name is kept equal to it).
	[[nodiscard]] String GetSectionName(const String& worldPath);

	/// @brief Asset path of the section definition next to a world: <dir>/<name>.hsecdef.
	[[nodiscard]] String GetSectionDefinitionPath(const String& worldPath);

	/// @brief Asset path of the baked section: one folder above the world's folder, <name>.hsec
	///        (Editor/Pcg/Sections/<name>.hsec for a section world).
	[[nodiscard]] String GetSectionAssetPath(const String& worldPath);

	/// @brief Whether the world is a section world: a .hsecdef sits next to its .hwld.
	[[nodiscard]] bool IsSectionWorld(const String& worldPath);

	/// @brief Checks a name for a new section: 1..64 characters of A-Z, a-z, 0-9, '_' and '-'.
	/// @param error Receives the reason if the name is not valid.
	bool IsValidSectionName(const String& name, String& error);

	/// @brief Creates a section world: the .hwld (terrain on, the given default material, sea level 0),
	///        the 2x2 terrain pages around world origin (flat at height 0, paint SectionDefaultPaint, no
	///        water) and a .hsecdef with one variant at originX 0. Refuses to overwrite an existing world
	///        or definition. Files written before a failure are removed again.
	/// @param name Section name (see IsValidSectionName).
	/// @param terrainMaterial Asset path of the terrain material; empty uses DefaultSectionTerrainMaterial.
	/// @param error Receives the reason on failure.
	/// @return True when all files were written.
	bool CreateSectionWorld(const String& name, const String& terrainMaterial, String& error);

	/// @brief CreateSectionWorld in another folder (see GetSectionWorldPath(folder, name)); an empty
	///        folder means SectionWorldFolder.
	bool CreateSectionWorld(const String& folder, const String& name, const String& terrainMaterial, String& error);

	/// @brief Reads the .hsecdef next to a world.
	bool LoadSectionDefinition(const String& worldPath, pcg::SectionDefinition& out, String& error);

	/// @brief Writes the .hsecdef next to a world.
	bool SaveSectionDefinition(const String& worldPath, const pcg::SectionDefinition& def, String& error);

	/// @brief Deterministic FNV-1a 64 hash over the section world's source files, stored in the .hsec
	///        as sourceStamp so a stale bake can be detected.
	/// @details Hashed, in this order: the .hwld, the .hsecdef, then every file below the world's data
	///          folder (<dir>/<name>/) ending in .tile (terrain pages), .wobj (entities) or .hfol
	///          (authored foliage), sorted by asset path. Per file: the asset path, a 0 byte, the size as
	///          8 little-endian bytes and the contents. A missing .hwld or .hsecdef contributes its path
	///          and size 0. Other files (terrain LOD, minimaps, records) are not part of the stamp.
	/// @param worldPath Asset path of the .hwld.
	[[nodiscard]] uint64 ComputeSectionSourceStamp(const String& worldPath);

	/// @brief Reads the sourceStamp of a baked section asset.
	/// @return False if the file is missing or not a readable section asset.
	bool ReadSectionAssetStamp(const String& assetPath, uint64& stamp);

	/// @brief The texture key of one terrain paint layer: the albedo texture asset path that identifies
	///        the layer when section paint is matched against the layers of a target tile.
	/// @details The single place this is resolved, for the section bake and the stamping remap alike:
	///          1. the material's layer binding: GetLayerBinding(layer).albedoTextureParam, resolved
	///             through GetTextureParameter (a material instance answers with its own value or its
	///             parent's);
	///          2. if the binding names no parameter, the conventional parameter "Albedo0<layer + 1>"
	///             (Albedo01..Albedo04), resolved the same way.
	///          A binding that names a parameter without a value does not fall back.
	/// @param material The terrain material (may be null).
	/// @param layer Paint layer 0..3.
	/// @param parameter Receives the parameter that was used if not null (empty when none was found).
	/// @param fromBinding Receives whether the parameter came from the layer binding if not null.
	/// @return The texture asset path, or an empty string if the layer has none.
	[[nodiscard]] String GetTerrainLayerTextureKey(const MaterialPtr& material, uint8 layer, String* parameter = nullptr, bool* fromBinding = nullptr);

	/// @brief GetTerrainLayerTextureKey for all four layers. Layers without a key stay empty.
	/// @return False if the material is null.
	bool GetSectionLayerTextures(const MaterialPtr& material, std::array<String, 4>& out);

	/// @brief Footprint of a map entity along the run direction (+Z): the local bounds of its mesh (or of its
	///        world model) scaled and rotated by the entity's derived transform, projected onto +Z.
	/// @param entity The map entity.
	/// @param out Receives the key (unique id), the length and the start offset from the pivot.
	/// @return False if the entity has no mesh / world model or empty bounds.
	bool ComputeRunPieceExtent(const MapEntity& entity, pcg::SectionPieceExtent& out);

	/// @brief ComputeRunPieceExtent for every run member of the definition that is loaded in the world.
	/// @param out Cleared, then receives one extent per loaded member, sorted by key.
	void CollectRunPieceExtents(const WorldEditorInstance& world, const pcg::SectionDefinition& def, std::vector<pcg::SectionPieceExtent>& out);

	/// @brief Bakes an open section world into its .hsec (GetSectionAssetPath).
	/// @details Reads the .hsecdef, refuses if a variant rectangle (with side falloff) leaves the
	///          terrain or covers a page without terrain, loads the covered pages, collects every map
	///          entity and authored foliage instance as world-space placements, takes the layer textures
	///          from the terrain default material (GetTerrainLayerTextureKey, logged per layer), stamps the
	///          files (ComputeSectionSourceStamp), refuses if a layer without a texture key is painted
	///          (non-zero coverage with weight in that layer) in any variant, and writes the asset. The
	///          section name is the world's stem whatever the .hsecdef says. Entities and foliage the bake
	///          force-loaded are released afterwards (unless pages were already force-loaded). The world is not saved first: the bake captures what is loaded, and the stamp
	///          describes the files on disk, so callers with unsaved edits save before baking.
	/// @param world The open section world.
	/// @param error Receives the reason on failure.
	/// @return True when the asset was written.
	bool BakeSectionWorld(WorldEditorInstance& world, String& error);
}
