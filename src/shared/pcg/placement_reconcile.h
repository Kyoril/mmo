// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "owned_content.h"

#include <functional>
#include <vector>

namespace mmo::pcg
{
	/// Difference between the placements an oasis owns and the ones a new build wants.
	struct ReconcileResult
	{
		/// Unchanged, nothing to do.
		std::vector<OwnedPlacement> keep;
		/// Same key and asset, new transform (uniqueId kept, written holds the new placement).
		std::vector<OwnedPlacement> update;
		/// uniqueIds to delete.
		std::vector<uint64> remove;
		/// Need new uniqueIds.
		std::vector<Placement> create;
	};

	/// Diffs previous against next by key. exists(uniqueId, kind) tells whether the owned object
	/// still exists in the world; owned objects that were deleted by hand are recreated, never removed.
	/// Results are ordered by key.
	ReconcileResult Reconcile(const std::vector<OwnedPlacement>& previous, const std::vector<Placement>& next,
		const std::function<bool(uint64, PlacementKind)>& exists);
}
