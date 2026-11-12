// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "generated_content.h"

#include "base/typedefs.h"
#include "pcg/section_asset.h"
#include "pcg/section_path.h"
#include "pcg/section_path_record.h"

#include <optional>
#include <utility>
#include <vector>

namespace mmo
{
	class IWorldEditor;

	/// @brief Generator label in the names of the map entities a section path creates.
	extern const char* const SectionPathLabel;

	/// @brief Asset folder (with trailing slash) holding the baked section assets (*.hsec).
	extern const char* const SectionAssetFolder;

	/// @brief Facade height jitter a new wall gets with NewSectionPathOptions::mirror.
	constexpr float DefaultFacadeHeightJitter = 0.2f;

	/// @brief Settings of a new section path besides its points, asset and name.
	struct NewSectionPathOptions
	{
		/// @brief Seed of the path.
		uint64 seed = 1;
		/// @brief True for a section loop (closed spline, floor side inside).
		bool closed = false;
		/// @brief Height mode; unset = FollowSpline for a path, SmoothedGround for a loop.
		std::optional<pcg::SectionHeightMode> heightMode;
		/// @brief SmoothedGround window (m); unset keeps the default.
		std::optional<float> groundSmoothing;
		/// @brief Turns the mass layer on (with its default settings).
		bool mass = false;
		/// @brief Facade variety: mirrored copies and DefaultFacadeHeightJitter.
		bool mirror = false;
		/// @brief name=value parameter settings applied last (pcg::SetSectionPathParam).
		std::vector<std::pair<String, String>> settings;
	};

	/// @brief Fills the parameters of a new section path: seed, height mode, smoothing window, mass, facade variety,
	///        then the name=value settings in order.
	/// @param error Receives the reason when a setting is refused.
	/// @return False if a setting has an unknown name or an invalid value.
	bool ApplyNewSectionPathOptions(const NewSectionPathOptions& options, pcg::SectionPathParams& params, String& error);

	/// @brief One-line summary of a laid path: its length and its straight, inner and outer corner copies.
	[[nodiscard]] String DescribeSectionLayout(const pcg::SectionPathShape& shape);

	/// @brief Lists every baked section asset (Editor/Pcg/Sections/*.hsec and the biome sections
	///        Editor/Pcg/Biomes/<Biome>/Sections/*.hsec), sorted.
	[[nodiscard]] std::vector<String> ListSectionAssets();

	/// @brief Reads a baked section asset.
	/// @param assetPath Asset path of the .hsec.
	/// @param out Receives the asset.
	/// @param error Receives why the asset is missing or not readable.
	/// @return True if the asset was read.
	bool LoadSectionAsset(const String& assetPath, pcg::SectionAsset& out, String& error);

	/// @brief Asset path of the section world a section asset was baked from:
	///        <dir>/<name>/<name>.hwld for <dir>/<name>.hsec.
	[[nodiscard]] String GetSectionWorldPathForAsset(const String& assetPath);

	/// @brief Checks whether a section asset is older than its section world files: the world
	///        exists and ComputeSectionSourceStamp of it differs from the asset's sourceStamp.
	/// @param assetPath Asset path of the .hsec.
	/// @param asset The asset as read from assetPath.
	/// @param warning Receives a message when the call returns true.
	/// @return True if the asset is stale (a missing section world is not stale: nothing to compare).
	bool IsSectionAssetStale(const String& assetPath, const pcg::SectionAsset& asset, String& warning);

	/// @brief Prepares the shape of a section path on flat ground (height 0), for validation, the
	///        influence, the layout and the overlap check (none of them depends on the heights).
	/// @param record The section path.
	/// @param asset Its section asset.
	/// @param shape Receives the shape.
	/// @param error Receives why the path is invalid.
	/// @return False if the path or the asset is not valid.
	bool PrepareSectionPathProbe(const pcg::SectionPathRecord& record, const pcg::SectionAsset& asset, pcg::SectionPathShape& shape, String& error);

	/// @brief Rebuilds a section path: validates it (the world stays untouched if it is invalid),
	///        then hands its shape to ApplyGeneratedContent with GetInfluenceRect as the influence.
	///        Section paint layers are matched per target tile by texture (GetTerrainLayerTextureKey of
	///        the tile material's layers against asset.layerTextures); painted section layers without a
	///        match on a tile are skipped there and listed in report.warnings as (tile, layer) pairs.
	///        Updates the record's owned content (the caller stores the per-copy rects in builtRects).
	/// @param editor The world editor to work on. Must have a terrain.
	/// @param record The section path to rebuild.
	/// @param asset The section asset of the path (read by the caller, see LoadSectionAsset).
	/// @return The report.
	OasisBuildReport RebuildSectionPath(IWorldEditor& editor, pcg::SectionPathRecord& record, const pcg::SectionAsset& asset);

	/// @brief Removes everything a section path put into the world (RemoveGeneratedContent on its
	///        owned content). Leaves the record's owned content and builtRects empty (spline, asset
	///        and parameters are kept).
	/// @param editor The world editor to work on.
	/// @param record The section path whose content to delete.
	/// @return The report (removed = number of owned objects deleted).
	OasisBuildReport DeleteSectionPathContent(IWorldEditor& editor, pcg::SectionPathRecord& record);
}
