// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shared/client_data/proto_client/skills.pb.h"
#include "shared/client_data/proto_client/spells.pb.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Client skill entries parse the categories and localized names", "[client_data][skills]")
{
	proto_client::SkillEntry source;
	source.set_id(1);
	source.set_name("Tailoring");
	source.set_category(1);
	source.set_cost(0);
	source.add_spells(500);
	auto* category = source.add_categories();
	category->set_id(2);
	category->set_name("Bags");
	category->set_sortorder(20);
	auto* loc = source.add_name_loc();
	loc->set_locale(1);
	loc->set_value("Schneiderei");

	proto_client::SkillEntry parsed;
	REQUIRE(parsed.ParseFromString(source.SerializeAsString()));
	CHECK(parsed.categories_size() == 1);
	CHECK(parsed.categories(0).name() == "Bags");
	CHECK(parsed.name_loc(0).value() == "Schneiderei");
}

TEST_CASE("Client spell entries carry the recipe fields", "[client_data][skills]")
{
	proto_client::SpellEntry spell;
	spell.set_skill(1);
	spell.set_skillrequired(5);
	spell.set_skillgreen(30);
	spell.set_craftcategory(2);
	CHECK(spell.skillrequired() == 5);
	CHECK(spell.skillgreen() == 30);
	CHECK(spell.craftcategory() == 2);
}
