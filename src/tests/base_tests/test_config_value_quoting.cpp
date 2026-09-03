// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "base/utilities.h"

#include <string>
#include <vector>

using mmo::TokenizeString;
using mmo::QuoteConfigValue;
using mmo::IsWritableConfigValue;
using mmo::ParseConfigAssignment;

namespace
{
	/// Tokenizes a string and returns the resulting token list for terser assertions.
	std::vector<std::string> Tokenize(const std::string& input)
	{
		std::vector<std::string> tokens;
		TokenizeString(input, tokens);
		return tokens;
	}
}

TEST_CASE("TokenizeString splits on single spaces", "[tokenize_string]")
{
	REQUIRE(Tokenize("dataPath Data") == std::vector<std::string>({ "dataPath", "Data" }));
}

TEST_CASE("TokenizeString collapses repeated whitespace instead of emitting empty tokens", "[tokenize_string]")
{
	REQUIRE(Tokenize("dataPath  \t Data") == std::vector<std::string>({ "dataPath", "Data" }));
	REQUIRE(Tokenize("  dataPath Data  ") == std::vector<std::string>({ "dataPath", "Data" }));
}

TEST_CASE("TokenizeString keeps quoted values with whitespace in one token", "[tokenize_string]")
{
	REQUIRE(Tokenize("dataPath \"C:\\Program Files\\Game\\Data\"")
		== std::vector<std::string>({ "dataPath", "C:\\Program Files\\Game\\Data" }));
}

TEST_CASE("TokenizeString keeps an explicitly empty quoted token", "[tokenize_string]")
{
	REQUIRE(Tokenize("gxResolution \"\"") == std::vector<std::string>({ "gxResolution", "" }));
}

TEST_CASE("TokenizeString reads a doubled quote inside a quoted token as one quote", "[tokenize_string]")
{
	REQUIRE(Tokenize("say \"He said \"\"hi\"\" loudly\"")
		== std::vector<std::string>({ "say", "He said \"hi\" loudly" }));
}

TEST_CASE("TokenizeString does not read past the end on an unterminated quote", "[tokenize_string]")
{
	REQUIRE(Tokenize("dataPath \"C:\\Some Path") == std::vector<std::string>({ "dataPath", "C:\\Some Path" }));
}

TEST_CASE("QuoteConfigValue leaves values without whitespace or quotes untouched", "[tokenize_string]")
{
	REQUIRE(QuoteConfigValue("Data") == "Data");
	REQUIRE(QuoteConfigValue("1920x1080") == "1920x1080");
	REQUIRE(QuoteConfigValue("C:\\Games\\Client\\Data") == "C:\\Games\\Client\\Data");
}

TEST_CASE("QuoteConfigValue quotes values containing whitespace", "[tokenize_string]")
{
	REQUIRE(QuoteConfigValue("C:\\Program Files\\Game\\Data") == "\"C:\\Program Files\\Game\\Data\"");
	REQUIRE(QuoteConfigValue("a\tb") == "\"a\tb\"");
}

TEST_CASE("QuoteConfigValue quotes an empty value so it survives a round trip", "[tokenize_string]")
{
	REQUIRE(QuoteConfigValue("") == "\"\"");
}

TEST_CASE("QuoteConfigValue doubles embedded quotes", "[tokenize_string]")
{
	REQUIRE(QuoteConfigValue("He said \"hi\"") == "\"He said \"\"hi\"\"\"");
	REQUIRE(QuoteConfigValue("no_space\"quote") == "\"no_space\"\"quote\"");
}

TEST_CASE("TokenizeString merges a quoted run with the unquoted text around it", "[tokenize_string]")
{
	REQUIRE(Tokenize("abc\"def ghi\"jkl") == std::vector<std::string>({ "abcdef ghijkl" }));
}

TEST_CASE("QuoteConfigValue quotes any whitespace Trim would strip", "[tokenize_string]")
{
	// Trim uses std::isspace, so quoting has to trigger on the same set or a trailing vertical
	// tab silently disappears on the way back in.
	REQUIRE(QuoteConfigValue("a\vb") == "\"a\vb\"");
	REQUIRE(QuoteConfigValue("a\fb") == "\"a\fb\"");
	REQUIRE(QuoteConfigValue("a\rb") == "\"a\rb\"");
	REQUIRE(QuoteConfigValue("a\nb") == "\"a\nb\"");
}

TEST_CASE("IsWritableConfigValue rejects values a line based file cannot hold", "[tokenize_string]")
{
	REQUIRE(IsWritableConfigValue("C:\\Program Files\\Game\\Data"));
	REQUIRE(IsWritableConfigValue(""));
	REQUIRE_FALSE(IsWritableConfigValue("a\nb"));
	REQUIRE_FALSE(IsWritableConfigValue("a\r\nb"));
}

TEST_CASE("QuoteConfigValue output round trips through TokenizeString", "[tokenize_string]")
{
	const std::vector<std::string> values =
	{
		"Data",
		"C:\\Program Files\\Game\\Data",
		// A Windows path ending in a separator: the closing quote follows a backslash, which must
		// not be read as escaping it.
		"C:\\Program Files\\Game\\Data\\",
		"",
		" ",
		"\t",
		"\"",
		"He said \"hi\"",
		"trailing space ",
		"a\vb",
		"1920x1080",
		// Bytes above 0x7f must survive untouched.
		"\xc3\xa4\xc3\xb6\xc3\xbc Pfad",
	};

	for (const auto& value : values)
	{
		const std::vector<std::string> tokens = Tokenize("dataPath " + QuoteConfigValue(value));
		REQUIRE(tokens.size() == 2);
		REQUIRE(tokens[0] == "dataPath");
		REQUIRE(tokens[1] == value);
	}
}

TEST_CASE("ParseConfigAssignment splits a plain assignment", "[tokenize_string]")
{
	std::string name;
	std::string value;

	REQUIRE(ParseConfigAssignment("gxResolution 1920x1080", name, value));
	REQUIRE(name == "gxResolution");
	REQUIRE(value == "1920x1080");
}

TEST_CASE("ParseConfigAssignment unquotes a quoted value", "[tokenize_string]")
{
	std::string name;
	std::string value;

	REQUIRE(ParseConfigAssignment("dataPath \"C:\\Program Files\\Game\\Data\"", name, value));
	REQUIRE(name == "dataPath");
	REQUIRE(value == "C:\\Program Files\\Game\\Data");
}

TEST_CASE("ParseConfigAssignment keeps an unquoted value with whitespace whole", "[tokenize_string]")
{
	// This is what config files written before values were quoted look like. Truncating at the
	// first space is exactly the bug that made such an install fail to start.
	std::string name;
	std::string value;

	REQUIRE(ParseConfigAssignment("dataPath C:\\Program Files\\Game\\Data", name, value));
	REQUIRE(name == "dataPath");
	REQUIRE(value == "C:\\Program Files\\Game\\Data");
}

TEST_CASE("ParseConfigAssignment reads back an explicitly empty value", "[tokenize_string]")
{
	std::string name;
	std::string value;

	REQUIRE(ParseConfigAssignment("gxApi \"\"", name, value));
	REQUIRE(name == "gxApi");
	REQUIRE(value.empty());
}

TEST_CASE("ParseConfigAssignment rejects input without a name or a value", "[tokenize_string]")
{
	std::string name;
	std::string value;

	REQUIRE_FALSE(ParseConfigAssignment("", name, value));
	REQUIRE_FALSE(ParseConfigAssignment("   ", name, value));
	REQUIRE_FALSE(ParseConfigAssignment("dataPath", name, value));
	REQUIRE_FALSE(ParseConfigAssignment("dataPath   ", name, value));
}

TEST_CASE("A saved config line survives the full write and read round trip", "[tokenize_string]")
{
	// The loop the client actually performs: saveconfig writes "set <name> <value>", the config
	// script reader hands the line to the set command, which parses it back.
	const std::vector<std::pair<std::string, std::string>> settings =
	{
		{ "dataPath", "C:\\Program Files\\Game\\Data" },
		{ "dataPath", "Data" },
		{ "gxResolution", "1920x1080" },
		{ "gxApi", "" },
		{ "motd", "He said \"hi\"" },
	};

	for (const auto& [settingName, settingValue] : settings)
	{
		REQUIRE(IsWritableConfigValue(settingValue));

		const std::string line = "set " + settingName + " " + QuoteConfigValue(settingValue);

		// Console::ExecuteCommand splits the command off at the first space.
		const auto space = line.find(' ');
		REQUIRE(space != std::string::npos);
		REQUIRE(line.substr(0, space) == "set");

		std::string parsedName;
		std::string parsedValue;
		REQUIRE(ParseConfigAssignment(line.substr(space + 1), parsedName, parsedValue));
		REQUIRE(parsedName == settingName);
		REQUIRE(parsedValue == settingValue);
	}
}
