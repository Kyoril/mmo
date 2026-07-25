// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/quest_chain_gating.h"

#include <set>

using namespace mmo;

namespace
{
	/// Adds a quest entry with the given chain fields to a quest template list.
	proto::QuestEntry& addQuest(proto::Quests& quests, const uint32 id, const int32 nextQuestId = 0, const int32 exclusiveGroup = 0)
	{
		auto* entry = quests.add_entry();
		entry->set_id(id);
		entry->set_name("Quest " + std::to_string(id));
		if (nextQuestId != 0)
		{
			entry->set_nextquestid(nextQuestId);
		}
		if (exclusiveGroup != 0)
		{
			entry->set_exclusivegroup(exclusiveGroup);
		}
		return *entry;
	}

	/// Builds an isRewarded callback from a set of rewarded quest ids.
	auto rewardedFrom(const std::set<uint32>& rewarded)
	{
		return [&rewarded](const uint32 questId) { return rewarded.contains(questId); };
	}

	/// Builds an isActiveInLog callback from a set of active quest ids.
	auto activeFrom(const std::set<uint32>& active)
	{
		return [&active](const uint32 questId) { return active.contains(questId); };
	}
}

TEST_CASE("IsQuestLockedByBackLinks - quest without back-links is never locked", "[quest_chain_gating]")
{
	proto::Quests quests;
	addQuest(quests, 1);
	addQuest(quests, 2);

	const std::set<uint32> rewarded;
	REQUIRE_FALSE(IsQuestLockedByBackLinks(quests, 2, rewardedFrom(rewarded)));
}

TEST_CASE("IsQuestLockedByBackLinks - locked while the predecessor is unrewarded", "[quest_chain_gating]")
{
	proto::Quests quests;
	addQuest(quests, 1, 2);
	addQuest(quests, 2);

	const std::set<uint32> rewarded;
	REQUIRE(IsQuestLockedByBackLinks(quests, 2, rewardedFrom(rewarded)));
}

TEST_CASE("IsQuestLockedByBackLinks - unlocked once the predecessor is rewarded", "[quest_chain_gating]")
{
	proto::Quests quests;
	addQuest(quests, 1, 2);
	addQuest(quests, 2);

	const std::set<uint32> rewarded{ 1 };
	REQUIRE_FALSE(IsQuestLockedByBackLinks(quests, 2, rewardedFrom(rewarded)));
}

TEST_CASE("IsQuestLockedByBackLinks - forked predecessors act as alternatives", "[quest_chain_gating]")
{
	// Two race-forked breadcrumbs (1 and 2) both point at follow-up 3.
	proto::Quests quests;
	addQuest(quests, 1, 3);
	addQuest(quests, 2, 3);
	addQuest(quests, 3);

	const std::set<uint32> none;
	REQUIRE(IsQuestLockedByBackLinks(quests, 3, rewardedFrom(none)));

	const std::set<uint32> firstRewarded{ 1 };
	REQUIRE_FALSE(IsQuestLockedByBackLinks(quests, 3, rewardedFrom(firstRewarded)));

	const std::set<uint32> secondRewarded{ 2 };
	REQUIRE_FALSE(IsQuestLockedByBackLinks(quests, 3, rewardedFrom(secondRewarded)));
}

TEST_CASE("IsQuestLockedByBackLinks - self-reference does not lock", "[quest_chain_gating]")
{
	// Defensive: a quest naming itself as nextquestid must not deadlock itself.
	proto::Quests quests;
	addQuest(quests, 1, 1);

	const std::set<uint32> rewarded;
	REQUIRE_FALSE(IsQuestLockedByBackLinks(quests, 1, rewardedFrom(rewarded)));
}

TEST_CASE("IsQuestLockedByExclusiveGroup - zero group never locks", "[quest_chain_gating]")
{
	proto::Quests quests;
	const auto& entry = addQuest(quests, 1);
	addQuest(quests, 2);

	const std::set<uint32> rewarded{ 2 };
	const std::set<uint32> active{ 2 };
	REQUIRE_FALSE(IsQuestLockedByExclusiveGroup(quests, entry, rewardedFrom(rewarded), activeFrom(active)));
}

TEST_CASE("IsQuestLockedByExclusiveGroup - negative group never locks", "[quest_chain_gating]")
{
	proto::Quests quests;
	const auto& entry = addQuest(quests, 1, 0, -5);
	addQuest(quests, 2, 0, -5);

	const std::set<uint32> rewarded{ 2 };
	const std::set<uint32> active;
	REQUIRE_FALSE(IsQuestLockedByExclusiveGroup(quests, entry, rewardedFrom(rewarded), activeFrom(active)));
}

TEST_CASE("IsQuestLockedByExclusiveGroup - locked while another member is rewarded", "[quest_chain_gating]")
{
	proto::Quests quests;
	const auto& entry = addQuest(quests, 1, 0, 7);
	addQuest(quests, 2, 0, 7);

	const std::set<uint32> rewarded{ 2 };
	const std::set<uint32> active;
	REQUIRE(IsQuestLockedByExclusiveGroup(quests, entry, rewardedFrom(rewarded), activeFrom(active)));
}

TEST_CASE("IsQuestLockedByExclusiveGroup - locked while another member is in the quest log", "[quest_chain_gating]")
{
	proto::Quests quests;
	const auto& entry = addQuest(quests, 1, 0, 7);
	addQuest(quests, 2, 0, 7);

	const std::set<uint32> rewarded;
	const std::set<uint32> active{ 2 };
	REQUIRE(IsQuestLockedByExclusiveGroup(quests, entry, rewardedFrom(rewarded), activeFrom(active)));
}

TEST_CASE("IsQuestLockedByExclusiveGroup - unlocked after the other member was abandoned", "[quest_chain_gating]")
{
	proto::Quests quests;
	const auto& entry = addQuest(quests, 1, 0, 7);
	addQuest(quests, 2, 0, 7);

	// Neither rewarded nor active anymore (abandoned).
	const std::set<uint32> rewarded;
	const std::set<uint32> active;
	REQUIRE_FALSE(IsQuestLockedByExclusiveGroup(quests, entry, rewardedFrom(rewarded), activeFrom(active)));
}

TEST_CASE("IsQuestLockedByExclusiveGroup - members of other groups do not lock", "[quest_chain_gating]")
{
	proto::Quests quests;
	const auto& entry = addQuest(quests, 1, 0, 7);
	addQuest(quests, 2, 0, 8);

	const std::set<uint32> rewarded{ 2 };
	const std::set<uint32> active;
	REQUIRE_FALSE(IsQuestLockedByExclusiveGroup(quests, entry, rewardedFrom(rewarded), activeFrom(active)));
}

TEST_CASE("IsQuestLockedByExclusiveGroup - own state never locks the quest itself", "[quest_chain_gating]")
{
	proto::Quests quests;
	const auto& entry = addQuest(quests, 1, 0, 7);

	// The quest itself being active/rewarded is handled by the caller's earlier checks and
	// must not count as a conflicting group member here.
	const std::set<uint32> rewarded{ 1 };
	const std::set<uint32> active{ 1 };
	REQUIRE_FALSE(IsQuestLockedByExclusiveGroup(quests, entry, rewardedFrom(rewarded), activeFrom(active)));
}
