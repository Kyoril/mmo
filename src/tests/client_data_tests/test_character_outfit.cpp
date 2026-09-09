// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/character_outfit.h"
#include "shared/client_data/proto_client/classes.pb.h"

using namespace mmo;
using namespace mmo::proto_client;

namespace
{
	/// Appends an outfit to a class entry. Pass -1 for race or gender to leave them wildcards.
	CharacterOutfit& AddOutfit(ClassEntry& classEntry, const int32 race, const int32 gender, const uint32 displayId)
	{
		CharacterOutfit* outfit = classEntry.add_outfits();
		outfit->set_race(race);
		outfit->set_gender(gender);
		outfit->add_item_displays(displayId);
		return *outfit;
	}
}

TEST_CASE("A class without outfits selects nothing", "[character_outfit]")
{
	ClassEntry classEntry;

	REQUIRE(SelectCharacterOutfit(classEntry, 0, 0) == nullptr);
}

TEST_CASE("A wildcard outfit matches every race and gender", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, -1, 100);

	const CharacterOutfit* human = SelectCharacterOutfit(classEntry, 0, 0);
	const CharacterOutfit* orcFemale = SelectCharacterOutfit(classEntry, 1, 1);

	REQUIRE(human != nullptr);
	REQUIRE(orcFemale != nullptr);
	REQUIRE(human->item_displays(0) == 100);
	REQUIRE(orcFemale->item_displays(0) == 100);
}

TEST_CASE("An outfit for another race is skipped", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, 1, -1, 200);

	REQUIRE(SelectCharacterOutfit(classEntry, 0, 0) == nullptr);
}

TEST_CASE("An outfit for another gender is skipped", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, 1, 200);

	REQUIRE(SelectCharacterOutfit(classEntry, 0, 0) == nullptr);
}

TEST_CASE("A race match beats the wildcard fallback", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, -1, 100);
	AddOutfit(classEntry, 2, -1, 300);

	const CharacterOutfit* selected = SelectCharacterOutfit(classEntry, 2, 0);

	REQUIRE(selected != nullptr);
	REQUIRE(selected->item_displays(0) == 300);
}

TEST_CASE("An exact race and gender match beats a race only match", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, -1, 100);
	AddOutfit(classEntry, 2, -1, 300);
	AddOutfit(classEntry, 2, 1, 400);

	const CharacterOutfit* female = SelectCharacterOutfit(classEntry, 2, 1);
	const CharacterOutfit* male = SelectCharacterOutfit(classEntry, 2, 0);

	REQUIRE(female != nullptr);
	REQUIRE(male != nullptr);
	REQUIRE(female->item_displays(0) == 400);
	REQUIRE(male->item_displays(0) == 300);
}

TEST_CASE("A gender only match beats the wildcard fallback", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, -1, 100);
	AddOutfit(classEntry, -1, 0, 500);

	const CharacterOutfit* selected = SelectCharacterOutfit(classEntry, 1, 0);

	REQUIRE(selected != nullptr);
	REQUIRE(selected->item_displays(0) == 500);
}

TEST_CASE("The first entry wins when two candidates score the same", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, 0, -1, 600);
	AddOutfit(classEntry, 0, -1, 700);

	const CharacterOutfit* selected = SelectCharacterOutfit(classEntry, 0, 0);

	REQUIRE(selected != nullptr);
	REQUIRE(selected->item_displays(0) == 600);
}

TEST_CASE("An unset race field defaults to the wildcard", "[character_outfit]")
{
	ClassEntry classEntry;
	CharacterOutfit* outfit = classEntry.add_outfits();
	outfit->add_item_displays(800);

	const CharacterOutfit* selected = SelectCharacterOutfit(classEntry, 2, 1);

	REQUIRE(selected != nullptr);
	REQUIRE(selected->item_displays(0) == 800);
}
