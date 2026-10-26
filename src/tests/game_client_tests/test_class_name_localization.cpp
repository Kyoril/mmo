// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/class_name_localizer.h"

#include <map>
#include <string>

using namespace mmo;

namespace
{
	/// Builds a lookup over a fixed translation table, mimicking Localization::FindStringById.
	LocalizationLookup MakeLookup(const std::map<std::string, std::string>& translations)
	{
		return [&translations](const std::string& key) -> const std::string*
		{
			const auto it = translations.find(key);
			return it != translations.end() ? &it->second : nullptr;
		};
	}
}

TEST_CASE("Class name localization key is derived from the base class name", "[class_name_localization]")
{
	CHECK(GetClassNameLocalizationKey("Scout") == "CLASS_SCOUT");
	CHECK(GetClassNameLocalizationKey("Warrior") == "CLASS_WARRIOR");
	CHECK(GetClassNameLocalizationKey("Shadow Priest") == "CLASS_SHADOW_PRIEST");
	CHECK(GetClassNameLocalizationKey("").empty());
}

TEST_CASE("Class names are translated through Localization.txt", "[class_name_localization]")
{
	// The deDE locale translates every class name (data/client/Locales/Locale_deDE/Localization.txt).
	const std::map<std::string, std::string> deDE{
		{ "CLASS_SCOUT", "Spaeher" },
		{ "CLASS_WARRIOR", "Krieger" },
	};

	const std::string scout = "Scout";
	CHECK(LocalizeClassName(scout, MakeLookup(deDE)) == "Spaeher");

	const std::string warrior = "Warrior";
	CHECK(LocalizeClassName(warrior, MakeLookup(deDE)) == "Krieger");
}

TEST_CASE("Class names without a translation keep the base name", "[class_name_localization]")
{
	const std::map<std::string, std::string> partial{
		{ "CLASS_SCOUT", "Spaeher" },
		{ "CLASS_CLERIC", "" },	// An empty translation must not blank out the displayed name.
	};

	const std::string acolyte = "Acolyte";
	CHECK(LocalizeClassName(acolyte, MakeLookup(partial)) == "Acolyte");

	const std::string cleric = "Cleric";
	CHECK(LocalizeClassName(cleric, MakeLookup(partial)) == "Cleric");

	// A missing lookup (no localization loaded) falls back to the base name as well.
	const std::string scout = "Scout";
	CHECK(LocalizeClassName(scout, LocalizationLookup()) == "Scout");
}

TEST_CASE("Localized class name stays valid for the caller", "[class_name_localization]")
{
	// UnitHandle returns a const char* into the string this helper hands back, so the
	// reference must point at storage that outlives the call - never at a temporary.
	const std::map<std::string, std::string> deDE{ { "CLASS_SCOUT", "Spaeher" } };

	const std::string scout = "Scout";
	const std::string& translated = LocalizeClassName(scout, MakeLookup(deDE));
	CHECK(&translated == &deDE.at("CLASS_SCOUT"));

	const std::string unknown = "Acolyte";
	const std::string& base = LocalizeClassName(unknown, MakeLookup(deDE));
	CHECK(&base == &unknown);
}
