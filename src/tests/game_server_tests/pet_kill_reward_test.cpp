// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A pet fights for its owner: damage it deals tags the creature for the owner and counts as the
// owner's own for experience, quest kill credit and loot. A pet that dies grants nothing, and a
// pet of an NPC never earns a player anything.

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
				levelValues->add_attributes(10);
				levelValues->add_attributes(10);
				levelValues->add_attributes(10);
				levelValues->add_attributes(10);
				levelValues->add_attributes(10);

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

		/// Spawns an untagged creature, standing in for a guard or any other NPC that joins a
		/// player's fight.
		std::shared_ptr<GameCreatureS> SpawnCreature(const Vector3& position)
		{
			auto creature = world->CreateCreature(*preyEntry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);
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

namespace
{
	/// Spawns a pet of the given owner. Only the Owner field links it, as persistent pets do.
	std::shared_ptr<GameCreatureS> SpawnPetOf(KillRewardFixture& f, const GameUnitS& owner, const Vector3& position, const bool grantsRewards)
	{
		auto pet = f.SpawnCreature(position);
		pet->SetOwnerGuid(owner.GetGuid());
		pet->SetPetGroup(0);
		pet->SetGrantsKillRewards(grantsRewards);
		return pet;
	}
}

TEST_CASE_METHOD(KillRewardFixture, "A creature killed only by a pet rewards the pet's owner", "[pets][kill_reward]")
{
	const auto player = SpawnQuestingPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto pet = SpawnPetOf(*this, *player, Vector3(1.0f, 0.0f, 0.0f), true);
	const auto prey = SpawnCreature(Vector3(0.0f, 0.0f, 0.0f));

	prey->Damage(prey->GetHealth(), 0, pet.get(), damage_type::MagicalAbility);
	RunPosted();

	CHECK(player->Get<uint32>(object_fields::Xp) == CreatureXp);
	CHECK(watcher.killCredits == 1);
	CHECK(prey->IsLootRecipient(*player));
}

TEST_CASE_METHOD(KillRewardFixture, "Damage from a pet linked only by its owner counts as the owner's own", "[pets][kill_reward]")
{
	const auto player = SpawnQuestingPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto pet = SpawnPetOf(*this, *player, Vector3(1.0f, 0.0f, 0.0f), true);
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);

	prey->Damage(prey->GetHealth() / 4, 0, player.get(), damage_type::MagicalAbility);
	prey->Damage(prey->GetHealth(), 0, pet.get(), damage_type::MagicalAbility);
	RunPosted();

	CHECK(player->Get<uint32>(object_fields::Xp) == CreatureXp);
	CHECK(watcher.killCredits == 1);
}

TEST_CASE_METHOD(KillRewardFixture, "Killing a pet grants no rewards", "[pets][kill_reward]")
{
	const auto player = SpawnQuestingPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto owner = SpawnPlayer(Vector3(300.0f, 0.0f, 0.0f));

	// A persistent pet does not switch GrantsKillRewards off; being a pet is enough.
	const bool grants = GENERATE(false, true);
	const auto pet = SpawnPetOf(*this, *owner, Vector3(0.0f, 0.0f, 0.0f), grants);
	pet->AddLootRecipient(player->GetGuid());
	pet->Damage(pet->GetHealth(), 0, player.get(), damage_type::MagicalAbility);
	RunPosted();

	CHECK(player->Get<uint32>(object_fields::Xp) == 0);
	CHECK(watcher.killCredits == 0);
}

TEST_CASE_METHOD(KillRewardFixture, "A creature killed by an NPC's pet rewards nobody", "[pets][kill_reward]")
{
	const auto player = SpawnQuestingPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto master = SpawnCreature(Vector3(3.0f, 0.0f, 0.0f));
	const auto pet = SpawnPetOf(*this, *master, Vector3(1.0f, 0.0f, 0.0f), true);
	const auto prey = SpawnTaggedPrey(Vector3(0.0f, 0.0f, 0.0f), *player);

	// The player pulls the creature and the NPC's pet does the work.
	prey->Damage(prey->GetHealth() / 4, 0, player.get(), damage_type::MagicalAbility);
	prey->Damage(prey->GetHealth(), 0, pet.get(), damage_type::MagicalAbility);
	RunPosted();

	CHECK(player->Get<uint32>(object_fields::Xp) == 0);
	CHECK(watcher.killCredits == 0);
}

TEST_CASE_METHOD(KillRewardFixture, "An untagged creature killed by an NPC's pet rewards nobody", "[pets][kill_reward]")
{
	const auto player = SpawnQuestingPlayer(Vector3(2.0f, 0.0f, 0.0f));
	const auto master = SpawnCreature(Vector3(3.0f, 0.0f, 0.0f));
	const auto pet = SpawnPetOf(*this, *master, Vector3(1.0f, 0.0f, 0.0f), true);
	const auto prey = SpawnCreature(Vector3(0.0f, 0.0f, 0.0f));

	prey->Damage(prey->GetHealth(), 0, pet.get(), damage_type::MagicalAbility);
	RunPosted();

	CHECK(player->Get<uint32>(object_fields::Xp) == 0);
	CHECK(watcher.killCredits == 0);
	CHECK_FALSE(prey->IsTagged());
}
