// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <string>

namespace
{
	/// The SPELL_CAST_FAILED_* keys of a shipped string table.
	std::set<std::string> ReadSpellCastFailedKeys(const std::string& locale)
	{
		const std::string path = std::string(MMO_CLIENT_LOCALES_DIR) + "/Locale_" + locale + "/Localization.txt";
		std::ifstream file(path);
		REQUIRE(file.is_open());

		static const std::regex keyPattern("key = \"(SPELL_CAST_FAILED_[A-Z0-9_]+)\"");
		std::set<std::string> keys;
		std::string line;
		while (std::getline(file, line))
		{
			std::smatch match;
			if (std::regex_search(line, match, keyPattern))
			{
				keys.insert(match[1].str());
			}
		}

		return keys;
	}
}

TEST_CASE("Every spell cast error is localized in every locale", "[pets][localization]")
{
	// Pet casts report these constantly (out of range, line of sight, ...); a missing key shows
	// up as the raw key in the error frame.
	const std::set<std::string> english = ReadSpellCastFailedKeys("enUS");
	REQUIRE(english.count("SPELL_CAST_FAILED_OUT_OF_RANGE") == 1);

	const char* locale = GENERATE("deDE", "frFR", "ruRU");
	CAPTURE(locale);
	const std::set<std::string> localized = ReadSpellCastFailedKeys(locale);
	for (const std::string& key : english)
	{
		CAPTURE(key);
		CHECK(localized.count(key) == 1);
	}
}
