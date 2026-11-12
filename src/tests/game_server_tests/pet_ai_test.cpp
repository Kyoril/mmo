// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Pet AI: a pet follows its owner on a slot beside it, joins the fights its owner picks (Assist)
// or only the ones it is told to (Passive), and goes back to its owner once the fight is over -
// without the reset-to-home and evade heal of an ordinary creature. Pet combat is the owner's
// combat: a fighting pet puts its owner in combat.

#include "catch.hpp"

#include "game_server/ai/creature_ai.h"
#include "game_server/ai/creature_ai_pet_state.h"
#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/pets/pet_behavior.h"
#include "game_server/pets/pet_manager.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "game/pet_defines.h"
#include "game/spell.h"
#include "game/spell_target_map.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "math/math_utils.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 PlayerTemplate = 1;
	constexpr uint32 MonsterTemplate = 2;
	constexpr uint32 PetEntryId = 10;
	constexpr uint32 MonsterEntryId = 11;
	constexpr uint32 SummonSpellId = 100;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	float Distance2D(const Vector3& a, const Vector3& b)
	{
		const float dx = a.x - b.x;
		const float dz = a.z - b.z;
		return std::sqrt(dx * dx + dz * dz);
	}

	struct PetAIFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		Universe universe{ io, timers };
		IdGenerator<uint64> objectIds{ 1 };
		NullTriggerHandler triggerHandler;

		std::unique_ptr<ConditionMgr> conditionMgr;
		std::unique_ptr<WorldInstanceManager> manager;
		WorldInstance* world = nullptr;

		proto::UnitEntry* petEntry = nullptr;
		proto::UnitEntry* monsterEntry = nullptr;
		proto::ClassEntry* classEntry = nullptr;
		proto::RaceEntry* raceEntry = nullptr;
		proto::SpellEntry* summonSpell = nullptr;
		uint64 nextPlayerGuid = 0x4000;

		PetAIFixture()
		{
			// Players and monsters are enemies. The pet entry carries the monster faction on
			// purpose: owner-aware friendliness makes it act with its owner's faction.
			auto* playerFaction = project.factionTemplates.add(PlayerTemplate);
			playerFaction->set_name("Players");
			playerFaction->set_flags(0);
			playerFaction->set_faction(1);
			playerFaction->add_enemies(2);

			auto* monsterFaction = project.factionTemplates.add(MonsterTemplate);
			monsterFaction->set_name("Monsters");
			monsterFaction->set_flags(0);
			monsterFaction->set_faction(2);
			monsterFaction->add_enemies(1);

			petEntry = AddUnitEntry(PetEntryId, "Wolf", MonsterTemplate);
			monsterEntry = AddUnitEntry(MonsterEntryId, "Boar", MonsterTemplate);

			classEntry = project.classes.add(1);
			classEntry->set_powertype(proto::ClassEntry_PowerType_MANA);
			for (uint32 i = 0; i < 5; ++i)
			{
				auto* levelValues = classEntry->add_levelbasevalues();
				levelValues->set_health(100);
				levelValues->set_mana(100);
				for (int a = 0; a < 5; ++a)
				{
					levelValues->add_attributes(10);
				}
				classEntry->add_xptonextlevel(100000);
			}

			raceEntry = project.races.add(1);
			raceEntry->set_name("Test Race");

			summonSpell = project.spells.add(SummonSpellId);
			summonSpell->set_name("Call Wolf");
			summonSpell->add_attributes(0);
			summonSpell->add_attributes(0);
			auto* effect = summonSpell->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::SummonPet);
			effect->set_targeta(spell_effect_targets::Caster);
			effect->set_summonunit(PetEntryId);
			effect->set_basepoints(1);

			// No map files exist for this directory: no nav mesh (every path fails), no collision.
			auto* map = project.maps.add(0);
			map->set_name("Pet AI Test");
			map->set_directory("Worlds/__pet_ai_test__/__pet_ai_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		proto::UnitEntry* AddUnitEntry(const uint32 id, const char* name, const uint32 factionTemplate)
		{
			auto* entry = project.units.add(id);
			entry->set_name(name);
			entry->set_minlevel(1);
			entry->set_maxlevel(1);
			entry->set_factiontemplate(factionTemplate);
			entry->set_malemodel(0);
			entry->set_femalemodel(0);
			entry->set_type(0);
			entry->set_family(0);
			entry->set_minlevelhealth(100);
			entry->set_maxlevelhealth(100);
			return entry;
		}

		std::shared_ptr<GamePlayerS> SpawnPlayer(const Vector3& position = Vector3(0.0f, 0.0f, 0.0f))
		{
			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			player->Set<uint64>(object_fields::Guid, nextPlayerGuid++, false);
			player->SetClass(*classEntry);
			player->SetRace(*raceEntry);
			player->SetLevel(1);
			player->Set<uint32>(object_fields::FactionTemplate, PlayerTemplate);
			player->Relocate(position, Radian(0.0f));
			world->AddGameObject(*player);
			return player;
		}

		/// Spawns a monster far enough away from everything that it does not pull on its own.
		std::shared_ptr<GameCreatureS> SpawnMonster(const Vector3& position = Vector3(35.0f, 0.0f, 0.0f))
		{
			auto creature = world->CreateCreature(*monsterEntry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);
			creature->GetAI()->Idle();
			return creature;
		}

		/// Spawns a temporary pet of an owner the way the SummonPet effect does.
		std::shared_ptr<GameCreatureS> SpawnPet(GameUnitS& owner, const Vector3& position = Vector3(2.0f, 0.0f, 0.0f))
		{
			auto pet = world->CreateTemporaryCreature(*petEntry, position, 0.0f, 0.0f);
			pet->SetGrantsKillRewards(false);
			pet->SetSummonOwner(std::static_pointer_cast<GameUnitS>(owner.shared_from_this()));
			pet->SetOwnerGuid(owner.GetGuid());
			pet->SetPetGroup(1);
			world->AddGameObject(*pet);
			owner.GetPets().AddTemporaryPet(*pet, 1, 5, pet_limit_policy::ReplaceOldest);
			pet->GetAI()->EnterPetMode(owner);
			return pet;
		}

		/// Spawns a pet that is no temporary pet (pet group 0), the way a persistent pet is set up.
		std::shared_ptr<GameCreatureS> SpawnPersistentPet(GameUnitS& owner, const Vector3& position = Vector3(2.0f, 0.0f, 0.0f))
		{
			auto pet = world->CreateTemporaryCreature(*petEntry, position, 0.0f, 0.0f);
			pet->SetGrantsKillRewards(false);
			pet->SetSummonOwner(std::static_pointer_cast<GameUnitS>(owner.shared_from_this()));
			pet->SetOwnerGuid(owner.GetGuid());
			pet->SetPetGroup(0);
			world->AddGameObject(*pet);
			owner.GetPets().SetPersistentPet(pet.get());
			pet->GetAI()->EnterPetMode(owner);
			return pet;
		}

		uint32 CountPetCreaturesInWorld() const
		{
			uint32 count = 0;
			world->ForEachObject([&count](GameObjectS& object)
				{
					if (object.IsUnit() && !object.IsPlayer() && object.Get<uint32>(object_fields::Entry) == PetEntryId)
					{
						++count;
					}
					return true;
				});
			return count;
		}

		GameCreatureS* FirstPetCreatureInWorld() const
		{
			GameCreatureS* result = nullptr;
			world->ForEachObject([&result](GameObjectS& object)
				{
					if (!result && object.IsUnit() && !object.IsPlayer() && object.Get<uint32>(object_fields::Entry) == PetEntryId)
					{
						result = static_cast<GameCreatureS*>(&object);
					}
					return true;
				});
			return result;
		}

		static bool InPetState(const GameCreatureS& pet)
		{
			return dynamic_cast<CreatureAIPetState*>(pet.GetAI()->GetCurrentState()) != nullptr;
		}

		/// The combat state is the only state besides the pet state a living pet uses, and it puts the pet in combat.
		static bool InCombatState(const GameCreatureS& pet)
		{
			return pet.GetAI()->GetCurrentState() != nullptr && !InPetState(pet) && pet.IsInCombat();
		}

		/// Runs the io service (posted work and timers) in real time for the given duration.
		void RunFor(const std::chrono::milliseconds duration)
		{
			const auto deadline = std::chrono::steady_clock::now() + duration;
			while (std::chrono::steady_clock::now() < deadline)
			{
				io.restart();
				io.run_for(std::chrono::milliseconds(10));
			}
		}

		/// Runs the io service until the condition holds or the timeout passes.
		template<typename Condition>
		bool RunUntil(Condition condition, const std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
		{
			const auto deadline = std::chrono::steady_clock::now() + timeout;
			while (!condition() && std::chrono::steady_clock::now() < deadline)
			{
				io.restart();
				io.run_for(std::chrono::milliseconds(10));
			}

			return condition();
		}

		~PetAIFixture()
		{
			io.stop();
		}
	};
}

// One AI update of the pet state: the follow / engage poll interval, plus slack for the timer queue.
static const std::chrono::milliseconds OneAIUpdate(PetUpdateInterval + 150);

TEST_CASE("PetFollowSlot puts slot 0 on the owner's right at 2 yd", "[pets][ai]")
{
	const Vector3 owner(10.0f, 3.0f, -4.0f);
	const float facings[] = { 0.0f, 0.7f, Pi * 0.5f, Pi, 4.0f };
	for (const float facing : facings)
	{
		for (uint32 count = 1; count <= 5; ++count)
		{
			const Vector3 slot = PetFollowSlot(owner, Radian(facing), 0, count);

			// The owner's right is a quarter turn clockwise from its forward direction.
			const Vector3 right = FacingToDirection(Radian(facing - Pi * 0.5f));
			const Vector3 expected = owner + right * 2.0f;
			CHECK(Distance2D(slot, expected) < 0.01f);
			CHECK(Distance2D(slot, owner) == Approx(2.0f).margin(0.01f));
		}
	}
}

TEST_CASE("PetFollowSlot slots of a swarm are apart from each other", "[pets][ai]")
{
	const Vector3 owner(0.0f, 0.0f, 0.0f);
	for (uint32 count = 2; count <= 10; ++count)
	{
		std::vector<Vector3> slots;
		for (uint32 i = 0; i < count; ++i)
		{
			slots.push_back(PetFollowSlot(owner, Radian(1.0f), i, count));
		}

		for (uint32 a = 0; a < count; ++a)
		{
			// Nobody stands on the owner.
			CHECK(Distance2D(slots[a], owner) >= 1.5f);
			for (uint32 b = a + 1; b < count; ++b)
			{
				INFO("count " << count << ", slots " << a << " and " << b);
				CHECK(Distance2D(slots[a], slots[b]) >= 1.0f);
			}
		}
	}
}

TEST_CASE("PetFollowSlot second and third slot are left and behind", "[pets][ai]")
{
	const Vector3 owner(0.0f, 0.0f, 0.0f);
	const Radian facing(0.0f);
	const Vector3 left = PetFollowSlot(owner, facing, 1, 3);
	const Vector3 behind = PetFollowSlot(owner, facing, 2, 3);

	CHECK(Distance2D(left, FacingToDirection(Radian(Pi * 0.5f)) * 2.0f) < 0.01f);
	CHECK(Distance2D(behind, FacingToDirection(Radian(Pi)) * 2.0f) < 0.01f);
}

TEST_CASE("ChoosePetTarget assist priority", "[pets][ai]")
{
	const std::vector<uint64> ownerAttackers = { 30, 31 };
	const std::vector<uint64> selfAttackers = { 40, 41 };
	const std::vector<uint64> none;

	CHECK(ChoosePetTarget(10, 20, ownerAttackers, selfAttackers, pet_strategy::Assist) == 10);
	CHECK(ChoosePetTarget(0, 20, ownerAttackers, selfAttackers, pet_strategy::Assist) == 20);
	CHECK(ChoosePetTarget(0, 0, ownerAttackers, selfAttackers, pet_strategy::Assist) == 30);
	CHECK(ChoosePetTarget(0, 0, none, selfAttackers, pet_strategy::Assist) == 40);
	CHECK(ChoosePetTarget(0, 0, none, none, pet_strategy::Assist) == 0);
}

TEST_CASE("ChoosePetTarget passive only follows commands", "[pets][ai]")
{
	const std::vector<uint64> ownerAttackers = { 30 };
	const std::vector<uint64> selfAttackers = { 40 };

	CHECK(ChoosePetTarget(10, 20, ownerAttackers, selfAttackers, pet_strategy::Passive) == 10);
	CHECK(ChoosePetTarget(0, 20, ownerAttackers, selfAttackers, pet_strategy::Passive) == 0);
}

TEST_CASE("PetRunSpeed keeps up with the owner", "[pets][ai]")
{
	CHECK(PetRunSpeed(7.0f, 9.0f) == Approx(9.0f));
	CHECK(PetRunSpeed(8.0f, 7.0f) == Approx(8.0f));
}

TEST_CASE_METHOD(PetAIFixture, "summoned pet runs the pet AI", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Self);
	REQUIRE(owner->CastSpell(targetMap, *summonSpell, 0, false, 0, true) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountPetCreaturesInWorld() == 1; }));

	GameCreatureS* pet = FirstPetCreatureInWorld();
	CHECK(pet->GetAI()->IsPetMode());
	CHECK(pet->GetAI()->GetPetOwner().get() == owner.get());
	CHECK(InPetState(*pet));
	CHECK_FALSE(pet->GetFollowedUnit());
}

TEST_CASE_METHOD(PetAIFixture, "owner attacking a creature makes the pet attack it", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));
	REQUIRE(pet->GetVictim() == nullptr);

	owner->StartAttack(monster);

	CHECK(RunUntil([&] { return pet->GetVictim() == monster.get(); }, OneAIUpdate));
	CHECK(InCombatState(*pet));
}

TEST_CASE_METHOD(PetAIFixture, "a creature attacking the owner is attacked by the persistent pet", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPersistentPet(*owner);
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));
	REQUIRE_FALSE(pet->GetAI()->IsPetOwnerTargetOnly());

	monster->GetAI()->EnterCombat(*owner);

	CHECK(RunUntil([&] { return pet->GetVictim() == monster.get(); }, OneAIUpdate));
}

TEST_CASE_METHOD(PetAIFixture, "a temporary pet ignores a creature attacking its owner", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));
	REQUIRE(pet->GetAI()->IsPetOwnerTargetOnly());

	monster->GetAI()->EnterCombat(*owner);
	RunFor(OneAIUpdate * 2);

	CHECK(pet->GetVictim() == nullptr);
	CHECK(InPetState(*pet));
}

TEST_CASE_METHOD(PetAIFixture, "a temporary pet does not fight back a creature attacking it", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));

	monster->GetAI()->EnterCombat(*pet);
	RunFor(OneAIUpdate * 2);

	CHECK(pet->GetVictim() == nullptr);
	CHECK(InPetState(*pet));
}

TEST_CASE_METHOD(PetAIFixture, "a temporary pet sticks to its owner's target while others attack it", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto target = SpawnMonster();
	const auto other = SpawnMonster(Vector3(-35.0f, 0.0f, 0.0f));
	RunFor(std::chrono::milliseconds(50));

	owner->StartAttack(target);
	REQUIRE(RunUntil([&] { return pet->GetVictim() == target.get(); }, OneAIUpdate));

	other->GetAI()->EnterCombat(*pet);
	other->GetAI()->EnterCombat(*owner);
	RunFor(OneAIUpdate * 2);

	CHECK(pet->GetVictim() == target.get());
}

TEST_CASE_METHOD(PetAIFixture, "a temporary pet switches to its owner's new target", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto first = SpawnMonster();
	const auto second = SpawnMonster(Vector3(-35.0f, 0.0f, 0.0f));
	RunFor(std::chrono::milliseconds(50));

	owner->StartAttack(first);
	REQUIRE(RunUntil([&] { return pet->GetVictim() == first.get(); }, OneAIUpdate));

	owner->StartAttack(second);
	CHECK(RunUntil([&] { return pet->GetVictim() == second.get(); }, OneAIUpdate));
}

TEST_CASE_METHOD(PetAIFixture, "a temporary pet summoned into a fight attacks its owner's target", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));

	// The owner attacks before it has any pet.
	owner->StartAttack(monster);
	const auto pet = SpawnPet(*owner);

	CHECK(RunUntil([&] { return pet->GetVictim() == monster.get(); }, OneAIUpdate * 2));
}

TEST_CASE_METHOD(PetAIFixture, "a temporary pet returns to its owner when the owner's target dies", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto target = SpawnMonster();
	const auto other = SpawnMonster(Vector3(-35.0f, 0.0f, 0.0f));
	RunFor(std::chrono::milliseconds(50));

	owner->StartAttack(target);
	REQUIRE(RunUntil([&] { return pet->GetVictim() == target.get(); }, OneAIUpdate));
	other->GetAI()->EnterCombat(*pet);
	RunFor(std::chrono::milliseconds(50));

	target->Kill(owner.get());

	CHECK(RunUntil([&] { return InPetState(*pet); }, OneAIUpdate * 3));
	CHECK(pet->GetVictim() == nullptr);
}

TEST_CASE_METHOD(PetAIFixture, "assist never pulls", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster(Vector3(25.0f, 0.0f, 0.0f));

	RunFor(std::chrono::milliseconds(1200));

	CHECK(pet->GetVictim() == nullptr);
	CHECK(InPetState(*pet));
	CHECK_FALSE(monster->IsInCombat());
	CHECK_FALSE(owner->IsInCombat());
}

TEST_CASE_METHOD(PetAIFixture, "passive pet ignores the owner's fight", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster();
	owner->GetPets().Command(pet_command::SetStrategy, 0, pet_strategy::Passive);
	REQUIRE(pet->GetAI()->GetPetStrategy() == pet_strategy::Passive);

	owner->StartAttack(monster);
	monster->GetAI()->EnterCombat(*owner);
	RunFor(OneAIUpdate * 2);
	CHECK(pet->GetVictim() == nullptr);

	// A command still makes it fight.
	owner->GetPets().Command(pet_command::Attack, monster->GetGuid(), 0);
	CHECK(RunUntil([&] { return pet->GetVictim() == monster.get(); }, OneAIUpdate));
}

TEST_CASE_METHOD(PetAIFixture, "a fighting pet puts its owner in combat", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));
	REQUIRE_FALSE(owner->IsInCombat());

	owner->GetPets().Command(pet_command::Attack, monster->GetGuid(), 0);

	REQUIRE(RunUntil([&] { return InCombatState(*pet); }, OneAIUpdate));
	CHECK(pet->IsInCombat());
	CHECK(owner->IsInCombat());
}

TEST_CASE_METHOD(PetAIFixture, "a pet follows its owner to its slot", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	RunFor(std::chrono::milliseconds(50));

	owner->Relocate(Vector3(10.0f, 0.0f, 0.0f), Radian(0.0f));

	const Vector3 slot = PetFollowSlot(owner->GetPosition(), owner->GetFacing(), 0, 1);
	CHECK(RunUntil([&] { return Distance2D(pet->GetMover().GetCurrentLocation(), slot) < 1.0f; }, OneAIUpdate * 3));
}

TEST_CASE_METHOD(PetAIFixture, "stay holds the pet in place until follow", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	RunFor(std::chrono::milliseconds(50));
	const Vector3 start = pet->GetMover().GetCurrentLocation();

	owner->GetPets().Command(pet_command::Stay, 0, 0);
	owner->Relocate(Vector3(10.0f, 0.0f, 0.0f), Radian(0.0f));
	RunFor(OneAIUpdate * 3);

	CHECK(Distance2D(pet->GetMover().GetCurrentLocation(), start) < 0.01f);
	CHECK(pet->GetAI()->IsPetStaying());

	owner->GetPets().Command(pet_command::Follow, 0, 0);
	CHECK_FALSE(pet->GetAI()->IsPetStaying());
	const Vector3 slot = PetFollowSlot(owner->GetPosition(), owner->GetFacing(), 0, 1);
	CHECK(RunUntil([&] { return Distance2D(pet->GetMover().GetCurrentLocation(), slot) < 1.0f; }, OneAIUpdate * 3));
}

TEST_CASE_METHOD(PetAIFixture, "a pet too far away teleports to its owner", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	RunFor(std::chrono::milliseconds(50));

	owner->Relocate(Vector3(150.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(RunUntil([&] { return Distance2D(pet->GetMover().GetCurrentLocation(), owner->GetPosition()) < 3.0f; }, OneAIUpdate * 2));
}

TEST_CASE_METHOD(PetAIFixture, "pet matches the owner's run speed", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	owner->SetBaseSpeed(movement_type::Run, 12.0f);
	owner->Relocate(Vector3(20.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(RunUntil([&] { return pet->GetSpeed(movement_type::Run) >= 12.0f - 0.01f; }, OneAIUpdate * 2));
}

TEST_CASE_METHOD(PetAIFixture, "when the fight is over the pet returns to its owner without evade heal", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));

	owner->StartAttack(monster);
	REQUIRE(RunUntil([&] { return InCombatState(*pet); }, OneAIUpdate));
	REQUIRE(owner->IsInCombat());

	pet->Set<uint32>(object_fields::Health, 40);
	owner->StopAttack();
	monster->Kill(nullptr);

	CHECK(RunUntil([&] { return InPetState(*pet); }, OneAIUpdate));
	CHECK(pet->Get<uint32>(object_fields::Health) == 40);
	CHECK_FALSE(pet->GetAI()->IsEvading());
	CHECK_FALSE(pet->IsInCombat());
	CHECK_FALSE(owner->IsInCombat());

	// And it follows again instead of walking home.
	owner->Relocate(Vector3(-15.0f, 0.0f, 5.0f), Radian(0.0f));
	const Vector3 slot = PetFollowSlot(owner->GetPosition(), owner->GetFacing(), 0, 1);
	CHECK(RunUntil([&] { return Distance2D(pet->GetMover().GetCurrentLocation(), slot) < 1.0f; }, OneAIUpdate * 3));
}

TEST_CASE_METHOD(PetAIFixture, "follow command calls a fighting pet back", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));

	owner->GetPets().Command(pet_command::Attack, monster->GetGuid(), 0);
	REQUIRE(RunUntil([&] { return InCombatState(*pet); }, OneAIUpdate));

	owner->GetPets().Command(pet_command::Follow, 0, 0);

	CHECK(RunUntil([&] { return InPetState(*pet); }, OneAIUpdate));
	CHECK(pet->GetVictim() == nullptr);
}

TEST_CASE_METHOD(PetAIFixture, "dismiss despawns temporary pets", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto a = SpawnPet(*owner);
	const auto b = SpawnPet(*owner, Vector3(-2.0f, 0.0f, 0.0f));

	owner->GetPets().Command(pet_command::Dismiss, 0, 0);

	CHECK(RunUntil([&] { return a->GetWorldInstance() == nullptr && b->GetWorldInstance() == nullptr; }));
}

TEST_CASE_METHOD(PetAIFixture, "npc owned pets join their owner's fight", "[pets][ai]")
{
	const auto player = SpawnPlayer(Vector3(35.0f, 0.0f, 0.0f));
	auto ownerCreature = world->CreateCreature(*monsterEntry, Vector3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f);
	world->AddGameObject(*ownerCreature);
	ownerCreature->GetAI()->Idle();
	const auto pet = SpawnPet(*ownerCreature);
	RunFor(std::chrono::milliseconds(50));

	ownerCreature->GetAI()->EnterCombat(*player);

	CHECK(RunUntil([&] { return pet->GetVictim() == player.get(); }, OneAIUpdate));
}

TEST_CASE_METHOD(PetAIFixture, "a pet beyond its leash returns to its owner instead of fighting from there", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto monster = SpawnMonster();

	// The owner already fights when the pet shows up 80 yd away: past the 60 yd leash, short of the
	// 100 yd teleport. Engaging from out there would be broken off at once, over and over.
	owner->StartAttack(monster);
	const auto pet = SpawnPet(*owner, Vector3(-80.0f, 0.0f, 0.0f));

	CHECK(RunUntil([&] { return Distance2D(pet->GetMover().GetCurrentLocation(), owner->GetPosition()) < 5.0f; }, OneAIUpdate * 3));

	// Back with its owner, it joins the fight.
	CHECK(RunUntil([&] { return pet->GetVictim() == monster.get(); }, OneAIUpdate * 2));
}

TEST_CASE_METHOD(PetAIFixture, "owner leaves combat when its fighting pet is dismissed", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPet(*owner);
	const auto monster = SpawnMonster();
	RunFor(std::chrono::milliseconds(50));

	owner->GetPets().Command(pet_command::Attack, monster->GetGuid(), 0);
	REQUIRE(RunUntil([&] { return InCombatState(*pet); }, OneAIUpdate));
	REQUIRE(owner->IsInCombat());

	owner->GetPets().Command(pet_command::Dismiss, 0, 0);

	CHECK(RunUntil([&] { return pet->GetWorldInstance() == nullptr; }));
	CHECK_FALSE(owner->IsInCombat());
}

TEST_CASE_METHOD(PetAIFixture, "owner leaves combat when its fighting pet's duration ends", "[pets][ai]")
{
	summonSpell->set_duration(1000);

	const auto owner = SpawnPlayer();
	const auto monster = SpawnMonster();
	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Self);
	REQUIRE(owner->CastSpell(targetMap, *summonSpell, 0, false, 0, true) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountPetCreaturesInWorld() == 1; }));
	const auto pet = std::static_pointer_cast<GameCreatureS>(FirstPetCreatureInWorld()->shared_from_this());

	owner->GetPets().Command(pet_command::Attack, monster->GetGuid(), 0);
	REQUIRE(RunUntil([&] { return InCombatState(*pet); }, OneAIUpdate));
	REQUIRE(owner->IsInCombat());

	CHECK(RunUntil([&] { return CountPetCreaturesInWorld() == 0; }, std::chrono::milliseconds(2000)));
	CHECK_FALSE(owner->IsInCombat());
}

TEST_CASE_METHOD(PetAIFixture, "an owned creature that is no pet does not put its owner in combat", "[pets][ai]")
{
	const auto owner = SpawnPlayer();
	const auto monster = SpawnMonster();

	// What the Summon effect does: an Owner, but no pet.
	auto add = world->CreateTemporaryCreature(*petEntry, Vector3(2.0f, 0.0f, 0.0f), 0.0f, 0.0f);
	add->SetOwnerGuid(owner->GetGuid());
	world->AddGameObject(*add);
	RunFor(std::chrono::milliseconds(50));

	add->GetAI()->EnterCombat(*monster);
	RunFor(std::chrono::milliseconds(100));

	REQUIRE(add->IsInCombat());
	CHECK_FALSE(owner->IsInCombat());
}

TEST_CASE_METHOD(PetAIFixture, "an attacked creature add that is no pet does not call its owner", "[pets][ai]")
{
	auto* guardEntry = AddUnitEntry(12, "Guard", PlayerTemplate);
	auto ownerCreature = world->CreateCreature(*monsterEntry, Vector3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f);
	world->AddGameObject(*ownerCreature);
	ownerCreature->GetAI()->Idle();

	auto add = world->CreateTemporaryCreature(*monsterEntry, Vector3(15.0f, 0.0f, 0.0f), 0.0f, 0.0f);
	add->SetOwnerGuid(ownerCreature->GetGuid());
	world->AddGameObject(*add);

	auto guard = world->CreateCreature(*guardEntry, Vector3(45.0f, 0.0f, 0.0f), 0.0f, 0.0f);
	world->AddGameObject(*guard);
	RunFor(std::chrono::milliseconds(50));

	guard->GetAI()->EnterCombat(*add);
	RunFor(std::chrono::milliseconds(300));

	CHECK_FALSE(ownerCreature->IsInCombat());
}
