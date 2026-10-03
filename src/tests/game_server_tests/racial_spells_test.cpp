// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/racial_spells.h"

#include "game/spell.h"
#include "shared/proto_data/project.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	proto::SpellEntry* AddSpell(proto::Project& project, const uint32 id, const uint32 attributesA)
	{
		proto::SpellEntry* spell = project.spells.add(id);
		spell->set_name("Spell " + std::to_string(id));
		spell->add_attributes(attributesA);
		spell->add_attributes(0);
		return spell;
	}
}

TEST_CASE("AppendRacialSpells appends known racial spells in list order", "[racial_spells]")
{
	proto::Project project;
	AddSpell(project, 248, spell_attributes::Passive);
	AddSpell(project, 250, spell_attributes::Ability);

	proto::RaceEntry race;
	race.add_racialspells(248);
	race.add_racialspells(250);

	std::vector<uint32> spellIds{ 37, 153 };
	const std::vector<uint32> added = AppendRacialSpells(race, project.spells, spellIds);

	CHECK(added == std::vector<uint32>{ 248, 250 });
	CHECK(spellIds == std::vector<uint32>{ 37, 153, 248, 250 });
}

TEST_CASE("AppendRacialSpells never duplicates a spell the character already knows", "[racial_spells]")
{
	proto::Project project;
	AddSpell(project, 248, spell_attributes::Passive);

	proto::RaceEntry race;
	race.add_racialspells(248);
	race.add_racialspells(248);

	std::vector<uint32> spellIds{ 248 };
	const std::vector<uint32> added = AppendRacialSpells(race, project.spells, spellIds);

	CHECK(added.empty());
	CHECK(spellIds == std::vector<uint32>{ 248 });
}

TEST_CASE("AppendRacialSpells skips spell ids missing from the project", "[racial_spells]")
{
	proto::Project project;
	AddSpell(project, 249, spell_attributes::Passive);

	proto::RaceEntry race;
	race.add_racialspells(9999);
	race.add_racialspells(249);

	std::vector<uint32> spellIds;
	const std::vector<uint32> added = AppendRacialSpells(race, project.spells, spellIds);

	CHECK(added == std::vector<uint32>{ 249 });
	CHECK(spellIds == std::vector<uint32>{ 249 });
}

TEST_CASE("IsActionBarAbility accepts only visible non-passive abilities", "[racial_spells]")
{
	proto::Project project;
	CHECK(IsActionBarAbility(*AddSpell(project, 1, spell_attributes::Ability)));
	CHECK_FALSE(IsActionBarAbility(*AddSpell(project, 2, spell_attributes::Passive)));
	CHECK_FALSE(IsActionBarAbility(*AddSpell(project, 3, spell_attributes::Ability | spell_attributes::Passive)));
	CHECK_FALSE(IsActionBarAbility(*AddSpell(project, 4, spell_attributes::Ability | spell_attributes::HiddenClientSide)));
	CHECK_FALSE(IsActionBarAbility(*AddSpell(project, 5, 0)));

	proto::SpellEntry noAttributes;
	CHECK_FALSE(IsActionBarAbility(noAttributes));
}
