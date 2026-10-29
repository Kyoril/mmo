// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "base/id_generator.h"
#include "game/aura.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <memory>

using namespace mmo;

namespace
{
	/// Helper: build a minimal shared GamePlayerS with a class entry set up
	/// so RefreshStats() / SetLevel() work without asserting.
	std::shared_ptr<GamePlayerS> MakeUnit(proto::Project& project, TimerQueue& timers, uint32 level = 1)
	{
		auto* cls = project.classes.getById(1);
		if (!cls)
		{
			cls = project.classes.add(1);
		}

		if (cls)
		{
			cls->set_powertype(proto::ClassEntry_PowerType_MANA);
			while (static_cast<uint32>(cls->levelbasevalues_size()) < level + 1)
			{
				auto* lbv = cls->add_levelbasevalues();
				lbv->set_health(100);
				lbv->set_mana(100);
				lbv->add_attributes(10);
				lbv->add_attributes(10);
				lbv->add_attributes(10);
				lbv->add_attributes(10);
				lbv->add_attributes(10);
			}
		}

		auto unit = std::make_shared<GamePlayerS>(project, timers);
		unit->Initialize();
		if (cls)
		{
			unit->SetClass(*cls);
		}
		unit->SetLevel(level);
		return unit;
	}

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct GmWorldFixture
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

		GmWorldFixture()
		{
			// No map files exist for this directory: no nav mesh, no collision.
			auto* map = project.maps.add(0);
			map->set_name("GM Test");
			map->set_directory("Worlds/__gm_test__/__gm_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		std::shared_ptr<GamePlayerS> MakePlayer(const uint64 guid)
		{
			auto unit = MakeUnit(project, timers, 10);
			unit->Set<uint64>(object_fields::Guid, guid);
			unit->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
			return unit;
		}

		void RunPosted()
		{
			for (int i = 0; i < 4; ++i)
			{
				io.restart();
				io.poll();
			}
		}

		~GmWorldFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE("GM-invisible unit is hidden from players and visible to GMs", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	const auto player = MakeUnit(project, timers, 60);
	const auto otherGm = MakeUnit(project, timers, 10);
	otherGm->SetIsGameMaster(true);

	gm->SetGmInvisible(true);
	CHECK(gm->IsGmInvisible());
	CHECK(gm->GetVisibility() == unit_visibility::Off);

	player->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	otherGm->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	gm->Relocate(Vector3(1.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK_FALSE(gm->CanBeSeenBy(*player));
	CHECK(gm->CanBeSeenBy(*otherGm));
}

TEST_CASE("Visibility recomputation keeps a GM-invisible unit Off", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	gm->SetGmInvisible(true);

	// Aura add/remove and interrupt-removal all funnel into NotifyVisibilityChanged; without
	// any visibility aura it used to fall through to On.
	gm->NotifyVisibilityChanged();
	CHECK(gm->GetVisibility() == unit_visibility::Off);
}

TEST_CASE("Turning GM invisibility off restores normal visibility", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	gm->SetGmInvisible(true);
	gm->SetGmInvisible(false);

	CHECK_FALSE(gm->IsGmInvisible());
	CHECK(gm->GetVisibility() == unit_visibility::On);
}

TEST_CASE_METHOD(GmWorldFixture, "GM-invisible player is not counted by the instance", "[gm_invisibility]")
{
	const auto gm = MakePlayer(0x10);
	gm->SetGmInvisible(true);
	world->AddGameObject(*gm);
	CHECK(world->GetPlayerCount() == 0);

	const auto player = MakePlayer(0x11);
	world->AddGameObject(*player);
	CHECK(world->GetPlayerCount() == 1);

	world->RemoveGameObject(*gm);
	CHECK(world->GetPlayerCount() == 1);
	world->RemoveGameObject(*player);
	CHECK(world->GetPlayerCount() == 0);
}

TEST_CASE_METHOD(GmWorldFixture, "Toggling invisibility inside the instance keeps the player count consistent", "[gm_invisibility]")
{
	// Counted on entry, hidden afterwards: leaving must still uncount it exactly once.
	const auto gm = MakePlayer(0x20);
	world->AddGameObject(*gm);
	CHECK(world->GetPlayerCount() == 1);
	gm->SetGmInvisible(true);
	world->RemoveGameObject(*gm);
	CHECK(world->GetPlayerCount() == 0);

	// Not counted on entry, visible afterwards: leaving must not underflow or uncount others.
	const auto other = MakePlayer(0x21);
	world->AddGameObject(*other);
	const auto gm2 = MakePlayer(0x22);
	gm2->SetGmInvisible(true);
	world->AddGameObject(*gm2);
	CHECK(world->GetPlayerCount() == 1);
	gm2->SetGmInvisible(false);
	world->RemoveGameObject(*gm2);
	CHECK(world->GetPlayerCount() == 1);
}
