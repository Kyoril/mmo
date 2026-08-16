// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Stat-based creatures derive their stamina, strength and agility from the level table of their
// unit class. Each of the three must read its own column: a creature that sourced strength or
// agility from the stamina column would silently gain attack power and armor it was never
// authored with, and no data file would show the discrepancy.

#include "catch.hpp"

#include "game_server/objects/game_creature_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/units.pb.h"
#include "shared/proto_data/unit_classes.pb.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	// Deliberately distinct per column so a cross-wired read is unambiguous in the assertion.
	constexpr uint32 BaseStamina = 30;
	constexpr uint32 BaseStrength = 19;
	constexpr uint32 BaseAgility = 27;
	constexpr uint32 CreatureLevel = 2;

	struct StatFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		proto::UnitEntry* entry = nullptr;

		StatFixture()
		{
			auto* unitClass = project.unitClasses.add(1);
			unitClass->set_name("Test Warrior");
			unitClass->set_internalname("test_warrior");
			unitClass->set_powertype(proto::StatConstants_PowerType_RAGE);
			unitClass->set_basemeleeattacktime(2000);

			for (uint32 level = 0; level < CreatureLevel; ++level)
			{
				auto* values = unitClass->add_levelbasevalues();
				values->set_health(220);
				values->set_mana(0);
				values->set_stamina(BaseStamina);
				values->set_strength(BaseStrength);
				values->set_agility(BaseAgility);
				values->set_intellect(23);
				values->set_spirit(23);
			}

			// Armor scales off agility, attack power off strength — the two columns the bug
			// replaced with stamina.
			auto* armorSource = unitClass->add_armorstatsources();
			armorSource->set_statid(proto::StatConstants_StatType_AGILITY);
			armorSource->set_factor(2.0f);

			auto* attackPowerSource = unitClass->add_attackpowerstatsources();
			attackPowerSource->set_statid(proto::StatConstants_StatType_STRENGTH);
			attackPowerSource->set_factor(2.0f);

			entry = project.units.add(1);
			entry->set_name("Test Creature");
			entry->set_minlevel(CreatureLevel);
			entry->set_maxlevel(CreatureLevel);
			entry->set_unitclassid(1);
			entry->set_usestatbasedsystem(true);
			entry->set_elitestatmultiplier(1.0f);
			entry->set_basearmor(0);
			entry->set_armorperlevel(0.0f);
		}

		std::shared_ptr<GameCreatureS> MakeCreature()
		{
			// Mirrors WorldInstance::CreateCreature: SetEntry is what installs the entry the stat
			// calculation reads, so nothing may touch stats before it runs.
			auto creature = std::make_shared<GameCreatureS>(project, timers, *entry);
			creature->Initialize();
			creature->SetEntry(*entry);
			return creature;
		}
	};
}

TEST_CASE_METHOD(StatFixture, "Stat-based creature reads each stat from its own column", "[creature_stats]")
{
	const auto creature = MakeCreature();

	CHECK(creature->Get<uint32>(object_fields::StatStamina) == BaseStamina);
	CHECK(creature->Get<uint32>(object_fields::StatStrength) == BaseStrength);
	CHECK(creature->Get<uint32>(object_fields::StatAgility) == BaseAgility);
}

TEST_CASE_METHOD(StatFixture, "Stat-based creature armor and attack power follow their source stat", "[creature_stats]")
{
	const auto creature = MakeCreature();

	// Only the amount above the 20-point floor contributes, per CalculateStatBasedStats.
	CHECK(creature->Get<uint32>(object_fields::Armor) == (BaseAgility - 20) * 2);

	// Strength sits below the floor here, so it must contribute nothing at all — which it cannot
	// do while it is being read from the (much higher) stamina column.
	CHECK(creature->Get<uint32>(object_fields::AttackPower) == 0);
}

TEST_CASE_METHOD(StatFixture, "Elite multiplier scales each stat column independently", "[creature_stats]")
{
	entry->set_elitestatmultiplier(2.0f);
	const auto creature = MakeCreature();

	CHECK(creature->Get<uint32>(object_fields::StatStamina) == BaseStamina * 2);
	CHECK(creature->Get<uint32>(object_fields::StatStrength) == BaseStrength * 2);
	CHECK(creature->Get<uint32>(object_fields::StatAgility) == BaseAgility * 2);
}
