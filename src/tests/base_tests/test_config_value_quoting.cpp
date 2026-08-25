// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "base/utilities.h"

#include <string>
#include <vector>

using mmo::TokenizeString;
using mmo::QuoteConfigValue;

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

TEST_CASE("QuoteConfigValue output round trips through TokenizeString", "[tokenize_string]")
{
	const std::vector<std::string> values =
	{
		"Data",
		"C:\\Program Files\\Game\\Data",
		"",
		"He said \"hi\"",
		"trailing space ",
		"1920x1080",
	};

	for (const auto& value : values)
	{
		const std::vector<std::string> tokens = Tokenize("dataPath " + QuoteConfigValue(value));
		REQUIRE(tokens.size() == 2);
		REQUIRE(tokens[0] == "dataPath");
		REQUIRE(tokens[1] == value);
	}
}
