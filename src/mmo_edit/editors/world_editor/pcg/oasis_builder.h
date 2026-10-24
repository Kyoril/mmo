// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "pcg/oasis.h"
#include "pcg/oasis_record.h"

#include <vector>

namespace mmo
{
	class IWorldEditor;

	/// @brief Outcome of an oasis rebuild or delete.
	struct OasisBuildReport
	{
		/// @brief False if the operation was refused or failed; error says why.
		bool ok = false;
		/// @brief Human readable reason when ok is false.
		String error;
		/// @brief Wall clock time the operation took.
		double milliseconds = 0.0;
		/// @brief Terrain samples (heights, splat pixels, water quads/heights, tile materials) the
		///        new contribution wrote.
		uint32 terrainSamples = 0;
		/// @brief Owned placements left as they were.
		uint32 kept = 0;
		/// @brief Owned placements moved to a new transform (same object).
		uint32 updated = 0;
		/// @brief Owned objects deleted.
		uint32 removed = 0;
		/// @brief Objects created.
		uint32 created = 0;
		/// @brief Terrain pages (x << 8 | z, the world page index) whose terrain data changed. Their
		///        far-terrain bake (.tlod) is stale: the world save does not re-bake it, run
		///        mmo_edit --bake-terrain-lod afterwards.
		std::vector<uint16> touchedPages;
	};

	/// @brief Rebuilds an oasis: validates its loop (the world stays untouched if it is invalid),
	///        loads the pages involved, removes the old terrain contribution, prepares the shape on
	///        the current ground, applies the new contribution and reconciles the placements
	///        (remove / update / create). Updates the record's influence, contribution and placements.
	/// @param editor The world editor to work on. Must have a terrain.
	/// @param record The oasis to rebuild.
	/// @param assets Asset lists and the terrain/water materials of the recipe.
	/// @return The report.
	OasisBuildReport RebuildOasis(IWorldEditor& editor, pcg::OasisRecord& record, const pcg::OasisAssets& assets);

	/// @brief Removes everything an oasis put into the world: its terrain contribution is undone and
	///        all owned entities and foliage instances are deleted. Leaves the record's contribution
	///        and placements empty (loop, seed and parameters are kept).
	/// @param editor The world editor to work on.
	/// @param record The oasis whose content to delete.
	/// @return The report (removed = number of owned objects deleted).
	OasisBuildReport DeleteOasisContent(IWorldEditor& editor, pcg::OasisRecord& record);
}
