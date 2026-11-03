// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "generated_content.h"

#include "base/typedefs.h"
#include "pcg/oasis.h"
#include "pcg/oasis_record.h"

namespace mmo
{
	class IWorldEditor;

	/// @brief Rebuilds an oasis: validates its loop (the world stays untouched if it is invalid),
	///        then hands its shape to ApplyGeneratedContent, which loads the pages involved, removes
	///        the old terrain contribution, prepares the shape on the current ground, applies the new
	///        contribution and reconciles the placements (remove / update / create). Updates the
	///        record's owned content.
	/// @param editor The world editor to work on. Must have a terrain.
	/// @param record The oasis to rebuild.
	/// @param assets Asset lists and the terrain/water materials of the recipe.
	/// @return The report.
	OasisBuildReport RebuildOasis(IWorldEditor& editor, pcg::OasisRecord& record, const pcg::OasisAssets& assets);

	/// @brief Removes everything an oasis put into the world (RemoveGeneratedContent on its owned
	///        content): its terrain contribution is undone and all owned entities and foliage
	///        instances are deleted. Leaves the record's owned content empty (loop, seed and
	///        parameters are kept).
	/// @param editor The world editor to work on.
	/// @param record The oasis whose content to delete.
	/// @return The report (removed = number of owned objects deleted).
	OasisBuildReport DeleteOasisContent(IWorldEditor& editor, pcg::OasisRecord& record);
}
