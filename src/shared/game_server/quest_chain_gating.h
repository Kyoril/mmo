// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "shared/proto_data/quests.pb.h"

namespace mmo
{
	// Both checks below scan the full quest template list linearly. This is fine at the current
	// project size (dozens to a few hundred quests); once the quest catalog grows into the
	// thousands, replace the scans with reverse-link indices (quest id -> predecessor ids,
	// exclusive group -> member ids) built once at project load.

	/// Determines whether a quest is locked by nextquestid back-links: at least one other quest
	/// names it as its nextquestid and none of those predecessor quests is rewarded. Multiple
	/// predecessors act as alternatives (OR semantics), so race/class-forked breadcrumb quests
	/// can all unlock the same follow-up.
	/// @param quests All quest templates.
	/// @param questId The quest whose availability is evaluated.
	/// @param isRewarded Callable (uint32 questId) -> bool returning whether the character has
	///        been rewarded for the given quest.
	/// @returns true if the quest is locked and must not be offered.
	template<typename IsRewardedFn>
	bool IsQuestLockedByBackLinks(const proto::Quests& quests, const uint32 questId, IsRewardedFn&& isRewarded)
	{
		bool hasBackLink = false;
		for (const auto& other : quests.entry())
		{
			if (other.id() != questId && other.nextquestid() == static_cast<int32>(questId))
			{
				hasBackLink = true;
				if (isRewarded(other.id()))
				{
					return false;
				}
			}
		}

		return hasBackLink;
	}

	/// Determines whether a quest is locked by its exclusive group: quests sharing the same
	/// POSITIVE group number are mutually exclusive, so the quest is locked while any OTHER
	/// member of its group is rewarded or currently active in the quest log. Negative groups
	/// are not supported and never lock.
	/// @param quests All quest templates.
	/// @param entry The quest whose availability is evaluated.
	/// @param isRewarded Callable (uint32 questId) -> bool returning whether the character has
	///        been rewarded for the given quest.
	/// @param isActiveInLog Callable (uint32 questId) -> bool returning whether the given quest
	///        currently occupies a quest log slot (incomplete, complete or failed).
	/// @returns true if the quest is locked and must not be offered.
	template<typename IsRewardedFn, typename IsActiveFn>
	bool IsQuestLockedByExclusiveGroup(const proto::Quests& quests, const proto::QuestEntry& entry, IsRewardedFn&& isRewarded, IsActiveFn&& isActiveInLog)
	{
		if (entry.exclusivegroup() <= 0)
		{
			return false;
		}

		for (const auto& other : quests.entry())
		{
			if (other.id() == entry.id() || other.exclusivegroup() != entry.exclusivegroup())
			{
				continue;
			}

			if (isRewarded(other.id()) || isActiveInLog(other.id()))
			{
				return true;
			}
		}

		return false;
	}
}
