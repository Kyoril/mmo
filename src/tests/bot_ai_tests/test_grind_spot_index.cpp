// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_ai/grind_spot_index.h"
#include "proto_data/project.h"

using namespace mmo;

namespace
{
	constexpr uint32 PlayerFactionTemplate = 1;
	constexpr uint32 PlayerFaction = 10;
	constexpr uint32 HostileFactionTemplate = 2;
	constexpr uint32 HostileFaction = 20;
	constexpr uint32 FriendlyFactionTemplate = 3;
	constexpr uint32 FriendlyFaction = 30;

	/// A project built in code rather than loaded from data/editor.
	///
	/// The index is a rule about spawn data, and testing a rule against live content couples the
	/// suite to whatever the content happens to say today - a flag edited in the data submodule
	/// would fail this suite for a reason that has nothing to do with the index.
	struct TestProject final
	{
		proto::Project project;

		TestProject()
		{
			auto* playerFaction = project.factionTemplates.add(PlayerFactionTemplate);
			playerFaction->set_name("player");
			playerFaction->set_flags(0);
			playerFaction->set_faction(PlayerFaction);
			playerFaction->set_selfmask(0x01);
			playerFaction->add_enemies(HostileFaction);

			auto* hostileFaction = project.factionTemplates.add(HostileFactionTemplate);
			hostileFaction->set_name("hostile");
			hostileFaction->set_flags(0);
			hostileFaction->set_faction(HostileFaction);
			hostileFaction->set_selfmask(0x02);
			hostileFaction->add_enemies(PlayerFaction);

			auto* friendlyFaction = project.factionTemplates.add(FriendlyFactionTemplate);
			friendlyFaction->set_name("friendly");
			friendlyFaction->set_flags(0);
			friendlyFaction->set_faction(FriendlyFaction);
			friendlyFaction->set_selfmask(0x04);

			auto* world = project.maps.add(0);
			world->set_name("world");
			world->set_directory("world");
			world->set_instancetype(proto::MapEntry_MapInstanceType_GLOBAL);

			auto* dungeon = project.maps.add(1);
			dungeon->set_name("dungeon");
			dungeon->set_directory("dungeon");
			dungeon->set_instancetype(proto::MapEntry_MapInstanceType_DUNGEON);
		}

		proto::UnitEntry& AddUnit(const uint32 id, const uint32 minLevel, const uint32 maxLevel, const uint32 factionTemplate)
		{
			auto* unit = project.units.add(id);
			unit->set_name("unit");
			unit->set_minlevel(minLevel);
			unit->set_maxlevel(maxLevel);
			unit->set_factiontemplate(factionTemplate);
			unit->set_malemodel(0);
			unit->set_femalemodel(0);
			unit->set_type(0);
			unit->set_family(0);
			return *unit;
		}

		void AddSpawn(const uint32 mapId, const uint32 unitEntry, const Vector3& position, const bool active = true)
		{
			auto* map = project.maps.getById(mapId);
			REQUIRE(map != nullptr);

			auto* spawn = const_cast<proto::MapEntry*>(map)->add_unitspawns();
			spawn->set_unitentry(unitEntry);
			spawn->set_positionx(position.x);
			spawn->set_positiony(position.y);
			spawn->set_positionz(position.z);
			spawn->set_isactive(active);
			spawn->set_maxcount(1);
		}
	};
}

TEST_CASE("The index keeps hostile normal creatures and nothing else", "[bot_ai][grind]")
{
	TestProject data;

	data.AddUnit(100, 4, 6, HostileFactionTemplate);
	data.AddSpawn(0, 100, Vector3(10.0f, 0.0f, 0.0f));

	// A vendor. The runtime target picker refuses anything with an NPC flag, so sending a bot
	// here would strand it next to something it will not fight.
	proto::UnitEntry& vendor = data.AddUnit(101, 4, 6, HostileFactionTemplate);
	vendor.set_npcflags(1);
	data.AddSpawn(0, 101, Vector3(11.0f, 0.0f, 0.0f));

	// An elite. A solo bot at its own level loses to it.
	proto::UnitEntry& elite = data.AddUnit(102, 4, 6, HostileFactionTemplate);
	elite.set_rank(1);
	data.AddSpawn(0, 102, Vector3(12.0f, 0.0f, 0.0f));

	// Friendly, so not a target at all.
	data.AddUnit(103, 4, 6, FriendlyFactionTemplate);
	data.AddSpawn(0, 103, Vector3(13.0f, 0.0f, 0.0f));

	// Disabled spawn point.
	data.AddUnit(104, 4, 6, HostileFactionTemplate);
	data.AddSpawn(0, 104, Vector3(14.0f, 0.0f, 0.0f), false);

	// Inside a dungeon, which a bot cannot walk into.
	data.AddUnit(105, 4, 6, HostileFactionTemplate);
	data.AddSpawn(1, 105, Vector3(15.0f, 0.0f, 0.0f));

	GrindSpotIndex index;
	index.Build(data.project, PlayerFactionTemplate);

	REQUIRE(index.GetSpotCount() == 1);
	CHECK(index.GetSpot(0).unitEntry == 100);
}

TEST_CASE("Candidates overlap the level band rather than being contained by it", "[bot_ai][grind]")
{
	TestProject data;

	data.AddUnit(100, 1, 2, HostileFactionTemplate);
	data.AddSpawn(0, 100, Vector3(1.0f, 0.0f, 0.0f));

	// Straddles the top of the band. Demanding containment would reject most of the world.
	data.AddUnit(101, 4, 6, HostileFactionTemplate);
	data.AddSpawn(0, 101, Vector3(2.0f, 0.0f, 0.0f));

	data.AddUnit(102, 20, 22, HostileFactionTemplate);
	data.AddSpawn(0, 102, Vector3(3.0f, 0.0f, 0.0f));

	GrindSpotIndex index;
	index.Build(data.project, PlayerFactionTemplate);

	const std::vector<std::size_t> candidates = index.FindCandidates(0, Vector3::Zero, 3, 5, 1000.0f, 10);

	REQUIRE(candidates.size() == 1);
	CHECK(index.GetSpot(candidates[0]).unitEntry == 101);
}

TEST_CASE("Candidates come back nearest first and respect the range limit", "[bot_ai][grind]")
{
	TestProject data;

	data.AddUnit(100, 4, 6, HostileFactionTemplate);
	data.AddSpawn(0, 100, Vector3(300.0f, 0.0f, 0.0f));
	data.AddSpawn(0, 100, Vector3(50.0f, 0.0f, 0.0f));
	data.AddSpawn(0, 100, Vector3(150.0f, 0.0f, 0.0f));
	data.AddSpawn(0, 100, Vector3(5000.0f, 0.0f, 0.0f));

	GrindSpotIndex index;
	index.Build(data.project, PlayerFactionTemplate);
	REQUIRE(index.GetSpotCount() == 4);

	const std::vector<std::size_t> candidates = index.FindCandidates(0, Vector3::Zero, 4, 6, 1000.0f, 10);

	REQUIRE(candidates.size() == 3);
	CHECK(index.GetSpot(candidates[0]).position.x == Approx(50.0f));
	CHECK(index.GetSpot(candidates[1]).position.x == Approx(150.0f));
	CHECK(index.GetSpot(candidates[2]).position.x == Approx(300.0f));
}

TEST_CASE("A query for an unknown map finds nothing rather than everything", "[bot_ai][grind]")
{
	TestProject data;
	data.AddUnit(100, 4, 6, HostileFactionTemplate);
	data.AddSpawn(0, 100, Vector3(10.0f, 0.0f, 0.0f));

	GrindSpotIndex index;
	index.Build(data.project, PlayerFactionTemplate);

	CHECK(index.FindCandidates(42, Vector3::Zero, 4, 6, 1000.0f, 10).empty());
}

TEST_CASE("Hostility follows the same rule the server applies", "[bot_ai][grind]")
{
	TestProject data;

	CHECK(IsFactionHostile(data.project, PlayerFactionTemplate, HostileFactionTemplate));
	CHECK_FALSE(IsFactionHostile(data.project, PlayerFactionTemplate, FriendlyFactionTemplate));
	CHECK_FALSE(IsFactionHostile(data.project, PlayerFactionTemplate, PlayerFactionTemplate));

	// An unknown faction template is not an excuse to attack: the server answers no here too.
	CHECK_FALSE(IsFactionHostile(data.project, PlayerFactionTemplate, 9999));
}

TEST_CASE("An explicit friend entry outranks the enemy mask", "[bot_ai][grind]")
{
	TestProject data;

	auto* aggressive = data.project.factionTemplates.add(50);
	aggressive->set_name("aggressive");
	aggressive->set_flags(0);
	aggressive->set_faction(50);
	aggressive->set_enemymask(0xFFFFFFFF);
	aggressive->add_friends(FriendlyFaction);

	CHECK_FALSE(IsFactionHostile(data.project, 50, FriendlyFactionTemplate));
	CHECK(IsFactionHostile(data.project, 50, HostileFactionTemplate));
}
