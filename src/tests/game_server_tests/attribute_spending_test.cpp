// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"

#include "game/attribute.h"
#include "game/item.h"
#include "game/object_type_id.h"
#include "game_server/character_data.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;
using namespace mmo::test;

// Attributes are game data (attributes.proto). Attribute points are spent per attribute id and
// saved together with the attribute layout they were spent under. When attributes are added or
// removed, the layout changes and the saved spending is refunded on the next login, so the points
// can be spent again; renaming an attribute must not refund anything.

namespace
{
	constexpr uint32 AgilityId = 2;
	constexpr uint32 SpiritId = 4;

	/// Builds a player whose class grants enough attribute points to buy a few.
	std::shared_ptr<GamePlayerS> MakeSpender(proto::Project& project, TimerQueue& timers)
	{
		auto unit = MakeUnit(project, timers);
		auto* cls = project.classes.getById(1);
		for (auto& levelValues : *cls->mutable_levelbasevalues())
		{
			levelValues.set_attributepoints(20);
		}
		unit->RefreshStats();
		return unit;
	}
}

TEST_CASE("The shipped attribute layout hash matches the value the database migration writes", "[attributes]")
{
	// data/realm/updates/20261029_1_data_driven_attributes.sql marks existing spending with this
	// hash. If it no longer matches, every character would lose their attribute points.
	proto::Attributes attributes;
	for (uint32 id = 0; id < 5; ++id)
	{
		attributes.add_entry()->set_id(id);
	}

	CHECK(ComputeAttributeLayoutHash(attributes) == 2393499409u);
}

TEST_CASE("The attribute layout hash ignores order and texts but not the set of ids", "[attributes]")
{
	proto::Attributes a;
	a.add_entry()->set_id(0);
	a.add_entry()->set_id(3);

	proto::Attributes b;
	auto* renamed = b.add_entry();
	renamed->set_id(3);
	renamed->set_name("Renamed");
	b.add_entry()->set_id(0);

	proto::Attributes c;
	c.add_entry()->set_id(0);
	c.add_entry()->set_id(3);
	c.add_entry()->set_id(5);

	CHECK(ComputeAttributeLayoutHash(a) == ComputeAttributeLayoutHash(b));
	CHECK(ComputeAttributeLayoutHash(a) != ComputeAttributeLayoutHash(c));
	CHECK(ComputeAttributeLayoutHash(proto::Attributes()) != 0);
}

TEST_CASE("Attribute points can only be spent on defined attributes", "[attributes]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeSpender(project, timers);

	CHECK(unit->AddAttributePoint(AgilityId));
	CHECK(unit->GetAttributePointsByAttribute(AgilityId) == 1);

	// Slot 7 exists in the fields but no attribute is defined there, slot 200 does not exist at all
	CHECK_FALSE(unit->AddAttributePoint(7));
	CHECK_FALSE(unit->AddAttributePoint(200));
}

TEST_CASE("Spent attribute points raise the attribute's replicated value", "[attributes]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeSpender(project, timers);

	const uint32 before = unit->Get<uint32>(object_fields::Stat + SpiritId);
	REQUIRE(unit->AddAttributePoint(SpiritId));
	CHECK(unit->Get<uint32>(object_fields::Stat + SpiritId) == before + 1);
}

TEST_CASE("Saved attribute spending is applied when the attribute layout is unchanged", "[attributes]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeSpender(project, timers);

	bool resetFired = false;
	unit->attributePointsReset.connect([&resetFired]() { resetFired = true; });

	CharacterClassData classData;
	classData.classId = 1;
	classData.attributePointsSpent[AgilityId] = 2;
	classData.attributeLayout = unit->GetAttributeLayout();

	CHECK(unit->ApplyAttributeSpending(classData));
	CHECK(unit->GetAttributePointsByAttribute(AgilityId) == 2);
	CHECK_FALSE(resetFired);
}

TEST_CASE("Saved attribute spending without a recorded layout is applied", "[attributes]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeSpender(project, timers);

	CharacterClassData classData;
	classData.classId = 1;
	classData.attributePointsSpent[AgilityId] = 1;
	classData.attributeLayout = 0;

	CHECK(unit->ApplyAttributeSpending(classData));
	CHECK(unit->GetAttributePointsByAttribute(AgilityId) == 1);
}

TEST_CASE("Saved attribute spending is refunded when attributes were added or removed", "[attributes]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeSpender(project, timers);
	const uint32 availableBefore = unit->Get<uint32>(object_fields::AvailableAttributePoints);

	CharacterClassData classData;
	classData.classId = 1;
	classData.attributePointsSpent[AgilityId] = 2;
	classData.attributeLayout = unit->GetAttributeLayout();

	// A sixth attribute is added after the points were spent
	project.attributes.add(5)->set_name("Luck");
	REQUIRE(unit->GetAttributeLayout() != classData.attributeLayout);

	bool resetFired = false;
	unit->attributePointsReset.connect([&resetFired]() { resetFired = true; });

	CHECK_FALSE(unit->ApplyAttributeSpending(classData));
	CHECK(resetFired);
	CHECK(unit->GetAttributePointsByAttribute(AgilityId) == 0);
	CHECK(unit->Get<uint32>(object_fields::AvailableAttributePoints) == availableBefore);
}

TEST_CASE("Saved attribute spending that no longer fits the budget is refunded entirely", "[attributes]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeSpender(project, timers);

	CharacterClassData classData;
	classData.classId = 1;
	classData.attributePointsSpent[AgilityId] = 1;
	classData.attributePointsSpent[SpiritId] = 500;
	classData.attributeLayout = unit->GetAttributeLayout();

	CHECK_FALSE(unit->ApplyAttributeSpending(classData));
	CHECK(unit->GetAttributePointsByAttribute(AgilityId) == 0);
	CHECK(unit->GetAttributePointsByAttribute(SpiritId) == 0);
}

TEST_CASE("Attribute point costs are replicated per attribute id", "[attributes]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeSpender(project, timers);

	for (uint32 attribute = 0; attribute < MaxAttributes; ++attribute)
	{
		unit->SetAttributeCost(attribute, static_cast<uint8>(attribute + 1));
	}
	for (uint32 attribute = 0; attribute < MaxAttributes; ++attribute)
	{
		CHECK(unit->GetAttributeCost(attribute) == attribute + 1);
	}
}

TEST_CASE("Item stat types map attribute ids into their own range", "[attributes]")
{
	CHECK(item_stat::FromAttribute(3) == item_stat::AttributeStart + 3);
	CHECK(item_stat::ToAttribute(item_stat::AttributeStart + 3) == 3);
	CHECK(item_stat::ToAttribute(item_stat::Mana) == -1);
	CHECK(item_stat::ToAttribute(item_stat::ExpertiseRating) == -1);

	// Unused item stat slots carry type 255 (-1 as uint8)
	CHECK(item_stat::ToAttribute(255) == -1);
}
