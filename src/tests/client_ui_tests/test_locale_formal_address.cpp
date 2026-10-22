// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	/// The shipped German string table under test.
	const std::string g_deDEPath = std::string(MMO_CLIENT_LOCALES_DIR) + "/Locale_deDE/Localization.txt";

	/// The informal second person address. The German locale addresses the player with the
	/// archaic formal "Ihr" / "Euer", so none of these may appear in a shipped string.
	const char* g_informalWords[] =
	{
		"du", "dir", "dich", "dein", "deine", "deinem", "deinen", "deiner", "deines"
	};

	/// Tells whether the given byte continues a word. Bytes of a multi byte UTF-8 sequence
	/// count as letters so that German umlauts do not look like a word boundary.
	bool IsWordByte(const unsigned char character)
	{
		if (character >= 0x80)
		{
			return true;
		}

		return std::isalpha(character) != 0 || character == '_';
	}

	/// Tells whether the line contains the given word, ignoring case and ignoring matches
	/// which are only part of a longer word.
	bool ContainsWord(const std::string& line, const std::string& word)
	{
		std::string haystack = line;
		for (char& character : haystack)
		{
			if (static_cast<unsigned char>(character) < 0x80)
			{
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			}
		}

		std::string::size_type offset = haystack.find(word);
		while (offset != std::string::npos)
		{
			const bool letterBefore = offset > 0 && IsWordByte(static_cast<unsigned char>(haystack[offset - 1]));
			const std::string::size_type after = offset + word.size();
			const bool letterAfter = after < haystack.size() && IsWordByte(static_cast<unsigned char>(haystack[after]));

			if (!letterBefore && !letterAfter)
			{
				return true;
			}

			offset = haystack.find(word, offset + 1);
		}

		return false;
	}
}

TEST_CASE("German locale addresses the player formally", "[localization][locale]")
{
	if (!std::filesystem::exists(g_deDEPath))
	{
		WARN("data/client is not checked out, skipping");
		return;
	}

	std::ifstream file(g_deDEPath, std::ios::binary);
	REQUIRE(file.is_open());

	std::vector<std::string> offenders;

	std::string line;
	while (std::getline(file, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}

		// Only the translated value matters, not the key, which is always English.
		const std::string::size_type valueStart = line.find("string = \"");
		if (valueStart == std::string::npos)
		{
			continue;
		}

		const std::string value = line.substr(valueStart);
		for (const char* word : g_informalWords)
		{
			if (ContainsWord(value, word))
			{
				offenders.push_back(line);
				break;
			}
		}
	}

	INFO("Strings using the informal address instead of \"Ihr\" / \"Euer\":\n" << [&offenders]()
		{
			std::ostringstream out;
			for (const std::string& offender : offenders)
			{
				out << offender << "\n";
			}
			return out.str();
		}());
	CHECK(offenders.empty());
}
