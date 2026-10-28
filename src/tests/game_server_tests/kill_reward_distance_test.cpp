// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Kill rewards belong to the players who are at the kill: the death state only reaches group
// members of a tagger within the reward radius around the corpse. The tagger itself bypassed the
// radius entirely, so a player who had died, released and respawned at the bind point - hundreds
// of yards away - still collected the experience, the quest kill credit and the corpse when the
// creature it had tagged died afterwards.
//
// The radius applies to all three. Being dead only costs the experience: a player who died to the
// creature it was killing keeps its quest kill credit and may loot the corpse once it is up again.

#include "catch.hpp"

#include "game_server/ai/creature_ai.h"
#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 PreyTemplate = 1;
	constexpr uint32 CreatureXp = 100;
	constexpr uint32 KillQuestId = 500;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	/// Counts the quest kill credits the player is told about, which is what a quest objective
	/// advancing looks like from outside GamePlayerS.
	struct KillCreditWatcher final : NetPlayerWatcher
	{
		void OnQuestKillCredit(const proto::QuestEntry&, uint64, uint32, uint32, uint32) override { ++killCredits; }

		void OnQuestItemCredit(const proto::QuestEntry&, uint32, uint32, uint32) override {}

		void OnQuestObjectRequirementMet(uint32) override {}

		void OnQuestDataChanged(uint32, const QuestStatusData&) override {}

		void OnQuestCompleted(uint64, uint32, uint32, uint32) override {}

		void OnItemAdded(uint16, uint16, bool, bool) override {}

		void OnObjectLoot() override {}

		uint32 killCredits = 0;
	};

	struct KillRewardFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		Universe universe{ io, timers };
		IdGenerator<uint64> objectIds{ 1 };
		NullTriggerHandler triggerHandler;

		// Declared before the world instance, so it outlives the players registered in it.
		KillCreditWatcher watcher;

		std::unique_ptr<ConditionMgr> conditionMgr;
		std::unique_ptr<WorldInstanceManager> manager;
		WorldInstance* world = nullptr;

		proto::UnitEntry* preyEntry = nullptr;
		proto::ClassEntry* classEntry = nullptr;
		proto::RaceEntry* raceEntry = nullptr;
		uint64 nextPlayerGuid = 0x4000;

		KillRewardFixture()
		{
			auto* factionTemplate = project.factionTemplates.add(PreyTemplate);
			factionTemplate->set_name("Test");
			factionTemplate->set_flags(0);
			factionTemplate->set_faction(1);

			preyEntry = project.units.add(1);
			preyEntry->set_name("Prey");
			preyEntry->set_minlevel(1);
			preyEntry->set_maxlevel(1);
			preyEntry->set_factiontemplate(PreyTemplate);
			preyEntry->set_malemodel(0);
			preyEntry->set_femalemodel(0);
			preyEntry->set_type(0);
			preyEntry->set_family(0);
			preyEntry->set_minlevelhealth(100);
			preyEntry->set_maxlevelhealth(100);
			preyEntry->set_minlevelxp(CreatureXp);
			preyEntry->set_maxlevelxp(CreatureXp);

			// A class without a class level curve: this suite is about character experience, and
			// RewardClassExperience is a no-op without one.
			classEntry = project.classes.add(1);
			classEntry->set_powertype(proto::ClassEntry_PowerType_MANA);
			for (uint32 i = 0; i < 5; ++i)
			{
				auto* levelValues = classEntry->add_levelbasevalues();
				levelValues->set_health(100);
				levelValues->set_mana(100);
				levelValues->set_stamina(10);
				levelValues->set_strength(10);
				levelValues->set_agility(10);
				levelValues->set_intellect(10);
				levelValues->set_spirit(10);

				// Far more than one kill grants, so no reward in this suite levels the character
				// up (which would consume the experience field this suite reads).
				classEntry->add_xptonextlevel(100000);
			}

			// Race and faction are irrelevant here, but GetQuestStatus asserts on a race entry.
			// Leaving its faction template unset keeps the players where they were before.
			raceEntry = project.races.add(1);
			raceEntry->set_name("Test Race");

			// Two kills needed, so a single kill only advances the objective instead of completing
			// the quest: this suite is about who gets credited, not about turning anything in.
			auto* quest = project.quests.add(KillQuestId);
			quest->set_name("Cull the Prey");
			quest->set_minlevel(1);
			auto* requirement = quest->add_requirements();
			requirement->set_creatureid(1);
			requirement->set_creaturecount(2);

			// No map files exist for this directory: no nav mesh, no collision, LOS is unblocked.
			auto* map = project.maps.add(0);
			map->set_name("Kill Reward Test");
			map->set_directory("Worlds/__kill_reward_test__/__kill_reward_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		/// Spawns a creature which is tagged by the given player, exactly as the combat state tags
		/// the first player to engage it.
		std::shared_ptr<GameCreatureS> SpawnTaggedPrey(const Vector3& position, const GamePlayerS& tagger)
		{
			auto creature = world->CreateCreature(*preyEntry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);
			creature->AddLootRecipient(tagger.GetGuid());
			return creature;
		}

		/// Adds a solo player at the given position to the world instance.
		std::shared_ptr<GamePlayerS> SpawnPlayer(const Vector3& position)
		{
			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			player->Set<uint64>(object_fields::Guid, nextPlayerGuid++, false);
			player->SetClass(*classEntry);
			player->SetRace(*raceEntry);
			player->SetLevel(1);
			player->Relocate(position, Radian(0.0f));
			world->AddGameObject(*player);
			return player;
		}

		/// Adds a solo player which has taken the kill quest and reports its credits to the
		/// fixture's watcher.
		std::shared_ptr<GamePlayerS> SpawnQuestingPlayer(const Vector3& position)
		{
			auto player = SpawnPlayer(position);
			player->SetPlayerWatcher(&watcher);
			REQUIRE(player->AcceptQuest(KillQuestId));
			return player;
		}

		/// Runs everything that was posted to the io service.
		void RunPosted()
		{
			for (int i = 0; i < 4; ++i)
			{
				io.restart();
				io.poll();
			}
		}

		~KillRewardFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(KillRewardFixture, "A player at the kill earns the creature's experience", "[kill_reward][xp]")
{
	const auto player = SpawnPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);
	REQUIRE(player->Get<uint32>(object_fields::Xp) == 0);

	prey->Damage(prey->GetHealth(), 0, player.get(), damage_type::MagicalAbility);
	RunPosted();

	CHECK(player->Get<uint32>(object_fields::Xp) == CreatureXp);
}

TEST_CASE_METHOD(KillRewardFixture, "A player far away when the creature dies earns no experience", "[kill_reward][xp]")
{
	// Roughly the distance between a camp and a bind point: the player died, released and is
	// nowhere near the creature it had tagged when that creature finally dies.
	const auto player = SpawnPlayer(Vector3(400.0f, 0.0f, 0.0f));
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);

	prey->Damage(prey->GetHealth(), 0, nullptr, damage_type::MagicalAbility);
	RunPosted();

	CHECK(player->Get<uint32>(object_fields::Xp) == 0);
}

TEST_CASE_METHOD(KillRewardFixture, "A dead player earns no experience from the creature it tagged", "[kill_reward][xp]")
{
	const auto player = SpawnPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);

	player->Set<uint32>(object_fields::Health, 0);
	REQUIRE_FALSE(player->IsAlive());

	prey->Damage(prey->GetHealth(), 0, nullptr, damage_type::MagicalAbility);
	RunPosted();

	CHECK(player->Get<uint32>(object_fields::Xp) == 0);
}

TEST_CASE_METHOD(KillRewardFixture, "A player at the kill owns the corpse and earns its quest kill credit", "[kill_reward][quest][loot]")
{
	const auto player = SpawnQuestingPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);

	prey->Damage(prey->GetHealth(), 0, player.get(), damage_type::MagicalAbility);
	RunPosted();

	CHECK(watcher.killCredits == 1);
	CHECK(prey->IsLootRecipient(*player));
}

TEST_CASE_METHOD(KillRewardFixture, "A dead player at the kill keeps its quest kill credit and the corpse", "[kill_reward][quest][loot]")
{
	const auto player = SpawnQuestingPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);

	// Killed by the very creature it was fighting: the objective was still worked for, and the
	// corpse has to be lootable once the player is back on its feet.
	player->Set<uint32>(object_fields::Health, 0);
	REQUIRE_FALSE(player->IsAlive());

	prey->Damage(prey->GetHealth(), 0, nullptr, damage_type::MagicalAbility);
	RunPosted();

	CHECK(watcher.killCredits == 1);
	CHECK(prey->IsLootRecipient(*player));
	CHECK(player->Get<uint32>(object_fields::Xp) == 0);
}

TEST_CASE_METHOD(KillRewardFixture, "A player far away when the creature dies earns no quest kill credit", "[kill_reward][quest]")
{
	const auto player = SpawnQuestingPlayer(Vector3(400.0f, 0.0f, 0.0f));
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);

	prey->Damage(prey->GetHealth(), 0, nullptr, damage_type::MagicalAbility);
	RunPosted();

	CHECK(watcher.killCredits == 0);
}

TEST_CASE_METHOD(KillRewardFixture, "A player far away when the creature dies does not own the corpse", "[kill_reward][loot]")
{
	const auto player = SpawnPlayer(Vector3(400.0f, 0.0f, 0.0f));
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);

	prey->Damage(prey->GetHealth(), 0, nullptr, damage_type::MagicalAbility);
	RunPosted();

	CHECK_FALSE(prey->IsLootRecipient(*player));
}
