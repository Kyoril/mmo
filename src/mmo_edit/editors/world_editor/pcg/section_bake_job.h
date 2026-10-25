// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/material.h"
#include "pcg/section_bake.h"

#include <array>

namespace mmo
{
	class WorldEditorInstance;

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

	/// @brief Albedo texture path per paint layer of a terrain material, from the material's layer
	///        bindings (albedoTextureParam) and its texture parameter values. Layers without a binding or
	///        value stay empty.
	/// @return False if the material is null.
	bool GetSectionLayerTextures(const MaterialPtr& material, std::array<String, 4>& out);

	/// @brief Bakes an open section world into its .hsec (GetSectionAssetPath).
	/// @details Reads the .hsecdef, refuses if a variant rectangle (with side falloff) leaves the
	///          terrain or covers a page without terrain, loads the covered pages, collects every map
	///          entity and authored foliage instance as world-space placements, takes the layer textures
	///          from the terrain default material, stamps the files (ComputeSectionSourceStamp) and writes
	///          the asset. The world is not saved first: the bake captures what is loaded, and the stamp
	///          describes the files on disk, so callers with unsaved edits save before baking.
	/// @param world The open section world.
	/// @param error Receives the reason on failure.
	/// @return True when the asset was written.
	bool BakeSectionWorld(WorldEditorInstance& world, String& error);
}
