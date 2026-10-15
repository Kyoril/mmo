// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A creature is tagged by the single player who first engaged it, but the kill rewards — and with
// them the corpse — belong to that player's nearby group members as well. The death state records
// everyone who was rewarded on the generated loot instance, so the corpse ownership check has to
// honour that list; otherwise a grouped player cannot loot anything a party mate tagged.

#include "catch.hpp"

#include "game_server/condition_mgr.h"
#include "game_server/loot_instance.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/loot_entry.pb.h"
#include "asio/io_service.hpp"

#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	struct LootRightsFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		ConditionMgr conditionMgr{ project.conditions };
		proto::UnitEntry* entry = nullptr;
		proto::LootEntry lootEntry;

		LootRightsFixture()
		{
			auto* itemEntry = project.items.add(1);
			itemEntry->set_id(1);
			itemEntry->set_maxstack(20);

			entry = project.units.add(1);
			entry->set_name("Test Creature");
			entry->set_minlevel(1);
			entry->set_maxlevel(1);

			auto* group = lootEntry.add_groups();
			auto* def = group->add_definitions();
			def->set_item(1);
			def->set_dropchance(100.0f);
			def->set_mincount(1);
			def->set_maxcount(1);
		}

		std::shared_ptr<GamePlayerS> MakePlayer(const uint64 guid)
		{
			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			player->Set<uint64>(object_fields::Guid, guid, false);
			return player;
		}

		std::shared_ptr<GameCreatureS> MakeCreature()
		{
			auto creature = std::make_shared<GameCreatureS>(project, timers, *entry);
			creature->Initialize();
			creature->SetEntry(*entry);
			return creature;
		}

		/// Builds the loot instance the death state would generate for the given reward recipients.
		std::unique_ptr<LootInstance> MakeLoot(const uint64 lootGuid, const std::vector<std::weak_ptr<GamePlayerS>>& recipients)
		{
			return std::make_unique<LootInstance>(
				project.items,
				conditionMgr,
				lootGuid,
				&lootEntry,
				0, 0,
				recipients,
				loot_method::GroupLoot,
				0);
		}
	};
}

TEST_CASE_METHOD(LootRightsFixture, "The player who tagged a creature may loot it", "[creature][loot_rights]")
{
	const auto tagger = MakePlayer(71);
	const auto creature = MakeCreature();
	creature->AddLootRecipient(tagger->GetGuid());
	creature->SetUnitLoot(MakeLoot(creature->GetGuid(), { tagger }));

	CHECK(creature->IsLootRecipient(*tagger));
}

TEST_CASE_METHOD(LootRightsFixture, "A group member rewarded for the kill may loot a creature a party mate tagged", "[creature][loot_rights]")
{
	const auto tagger = MakePlayer(71);
	const auto groupMember = MakePlayer(72);

	const auto creature = MakeCreature();

	// Only the player who first engaged the creature ends up in the tag list.
	creature->AddLootRecipient(tagger->GetGuid());

	// The death state rewards every nearby group member and records them all on the loot instance.
	creature->SetUnitLoot(MakeLoot(creature->GetGuid(), { tagger, groupMember }));

	CHECK(creature->IsLootRecipient(*groupMember));
}

TEST_CASE_METHOD(LootRightsFixture, "A player who neither tagged nor was rewarded may not loot a creature", "[creature][loot_rights]")
{
	const auto tagger = MakePlayer(71);
	const auto stranger = MakePlayer(99);

	const auto creature = MakeCreature();
	creature->AddLootRecipient(tagger->GetGuid());
	creature->SetUnitLoot(MakeLoot(creature->GetGuid(), { tagger }));

	CHECK_FALSE(creature->IsLootRecipient(*stranger));
}
