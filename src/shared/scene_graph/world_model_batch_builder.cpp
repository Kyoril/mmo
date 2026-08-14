// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_batch_builder.h"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace mmo
{
	bool WorldModelBatchKey::operator<(const WorldModelBatchKey& other) const
	{
		if (groupIndex != other.groupIndex)
		{
			return groupIndex < other.groupIndex;
		}

		if (meshPath != other.meshPath)
		{
			return meshPath < other.meshPath;
		}

		if (submeshIndex != other.submeshIndex)
		{
			return submeshIndex < other.submeshIndex;
		}

		return materialOverride < other.materialOverride;
	}

	void BuildWorldModelBuckets(
		const std::vector<WorldModelPlacementInput>& placements,
		const WorldModelMeshLookup& meshLookup,
		const size_t minInstancesPerBatch,
		std::vector<WorldModelBucket>& outBuckets,
		std::vector<WorldModelPlacementInput>& outSingletons)
	{
		outBuckets.clear();
		outSingletons.clear();

		if (!meshLookup)
		{
			outSingletons = placements;
			return;
		}

		// Ordered so the resulting bucket order is deterministic across runs, which keeps the batch
		// object names stable and makes golden-order assertions in tests meaningful.
		std::map<WorldModelBatchKey, size_t> bucketIndices;

		// Mesh resolution can hit the disk, so never look the same path up twice. The value is a
		// copy rather than a pointer because a lookup may invalidate whatever the previous call
		// returned.
		std::unordered_map<String, WorldModelMeshFacts> factsCache;
		std::unordered_map<String, bool> factsResolved;

		for (const auto& placement : placements)
		{
			auto resolvedIt = factsResolved.find(placement.meshPath);
			if (resolvedIt == factsResolved.end())
			{
				const WorldModelMeshFacts* facts = meshLookup(placement.meshPath);
				resolvedIt = factsResolved.emplace(placement.meshPath, facts != nullptr).first;
				if (facts)
				{
					factsCache[placement.meshPath] = *facts;
				}
			}

			if (!resolvedIt->second)
			{
				// Mesh could not be resolved at all - let the per-entity path deal with it (and log).
				outSingletons.push_back(placement);
				continue;
			}

			const WorldModelMeshFacts& facts = factsCache[placement.meshPath];
			if (!facts.batchable || facts.submeshCount == 0)
			{
				outSingletons.push_back(placement);
				continue;
			}

			// One bucket per submesh: a draw call covers exactly one index range and one material.
			for (uint16 submeshIndex = 0; submeshIndex < facts.submeshCount; ++submeshIndex)
			{
				WorldModelBatchKey key;
				key.groupIndex = placement.groupIndex;
				key.meshPath = placement.meshPath;
				key.submeshIndex = submeshIndex;
				key.materialOverride = placement.materialOverride;

				const auto [it, inserted] = bucketIndices.emplace(key, outBuckets.size());
				if (inserted)
				{
					WorldModelBucket bucket;
					bucket.key = key;
					outBuckets.push_back(std::move(bucket));
				}

				outBuckets[it->second].placements.push_back(placement);
			}
		}

		// Demote buckets too small to be worth an instanced draw. A placement is demoted once, not
		// once per submesh, because the per-entity path renders all of its submeshes together.
		std::vector<WorldModelBucket> keptBuckets;
		keptBuckets.reserve(outBuckets.size());

		for (auto& bucket : outBuckets)
		{
			if (bucket.placements.size() >= minInstancesPerBatch)
			{
				keptBuckets.push_back(std::move(bucket));
				continue;
			}

			if (bucket.key.submeshIndex == 0)
			{
				for (const auto& placement : bucket.placements)
				{
					outSingletons.push_back(placement);
				}
			}
		}

		// Restore key order: demotion above preserves relative order, but the buckets were appended
		// in first-seen order rather than key order.
		std::sort(keptBuckets.begin(), keptBuckets.end(),
			[](const WorldModelBucket& lhs, const WorldModelBucket& rhs) { return lhs.key < rhs.key; });

		outBuckets = std::move(keptBuckets);
	}
}
