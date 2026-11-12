// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "placement_reconcile.h"

#include <map>

namespace mmo::pcg
{
	ReconcileResult Reconcile(const std::vector<OwnedPlacement>& previous, const std::vector<Placement>& next,
		const std::function<bool(uint64, PlacementKind)>& exists)
	{
		ReconcileResult result;

		std::map<uint64, const OwnedPlacement*> prevByKey;
		for (const OwnedPlacement& p : previous)
		{
			prevByKey[p.key] = &p;
		}
		std::map<uint64, const Placement*> nextByKey;
		for (const Placement& p : next)
		{
			nextByKey[p.key] = &p;
		}

		for (const auto& entry : prevByKey)
		{
			const uint64 key = entry.first;
			const OwnedPlacement* prev = entry.second;
			const bool present = exists(prev->uniqueId, prev->kind);
			const auto nit = nextByKey.find(key);
			if (nit == nextByKey.end())
			{
				if (present)
				{
					result.remove.push_back(prev->uniqueId);
				}
				continue;
			}

			const Placement& want = *nit->second;
			if (prev->written.asset != want.asset || prev->kind != want.kind ||
				prev->written.kitPaths != want.kitPaths || prev->written.materialOverrides != want.materialOverrides)
			{
				if (present)
				{
					result.remove.push_back(prev->uniqueId);
				}
				result.create.push_back(want);
				continue;
			}

			if (!present)
			{
				result.create.push_back(want);
				continue;
			}

			const Placement& old = prev->written;
			OwnedPlacement owned = *prev;
			owned.written = want;
			if (old.position == want.position && old.rotation == want.rotation && old.scale == want.scale &&
				old.collides == want.collides)
			{
				result.keep.push_back(std::move(owned));
			}
			else
			{
				result.update.push_back(std::move(owned));
			}
		}

		for (const auto& entry : nextByKey)
		{
			if (prevByKey.find(entry.first) == prevByKey.end())
			{
				result.create.push_back(*entry.second);
			}
		}

		return result;
	}
}
