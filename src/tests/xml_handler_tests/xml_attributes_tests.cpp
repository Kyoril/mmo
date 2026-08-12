// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "xml_handler/xml_attributes.h"

using namespace mmo;

namespace
{
	XmlAttributes MakeAttributes(std::initializer_list<std::pair<const char*, const char*>> pairs)
	{
		XmlAttributes attrs;
		for (const auto& pair : pairs)
		{
			attrs.Add(pair.first, pair.second);
		}

		return attrs;
	}
}

TEST_CASE("XmlAttributes Add stores and overwrites by name", "[xml_handler][attributes]")
{
	XmlAttributes attrs;
	attrs.Add("name", "Button");
	CHECK(attrs.GetCount() == 1);
	CHECK(attrs.GetValue("name") == "Button");

	attrs.Add("name", "CheckBox");
	CHECK(attrs.GetCount() == 1);
	CHECK(attrs.GetValue("name") == "CheckBox");
}

TEST_CASE("XmlAttributes Exists and Remove", "[xml_handler][attributes]")
{
	auto attrs = MakeAttributes({ { "name", "Button" }, { "text", "OK" } });

	CHECK(attrs.Exists("name"));
	CHECK_FALSE(attrs.Exists("missing"));

	attrs.Remove("name");
	CHECK_FALSE(attrs.Exists("name"));
	CHECK(attrs.GetCount() == 1);

	// Removing something that is not there is a no-op rather than an error.
	attrs.Remove("missing");
	CHECK(attrs.GetCount() == 1);
}

TEST_CASE("XmlAttributes indexed access follows name order", "[xml_handler][attributes]")
{
	// Backed by a std::map, so index order is the attribute names sorted, not insertion
	// order. Anything iterating by index depends on that.
	auto attrs = MakeAttributes({ { "text", "OK" }, { "name", "Button" } });

	REQUIRE(attrs.GetCount() == 2);
	CHECK(attrs.GetName(0) == "name");
	CHECK(attrs.GetValue(0) == "Button");
	CHECK(attrs.GetName(1) == "text");
	CHECK(attrs.GetValue(1) == "OK");
}

TEST_CASE("XmlAttributes GetValueAsString falls back to the default",
	"[xml_handler][attributes]")
{
	auto attrs = MakeAttributes({ { "text", "OK" } });

	CHECK(attrs.GetValueAsString("text") == "OK");
	CHECK(attrs.GetValueAsString("missing").empty());
	CHECK(attrs.GetValueAsString("missing", "fallback") == "fallback");
}

TEST_CASE("XmlAttributes GetValueAsBool accepts both spellings", "[xml_handler][attributes]")
{
	auto attrs = MakeAttributes({
		{ "wordTrue", "true" },
		{ "wordFalse", "false" },
		{ "digitTrue", "1" },
		{ "digitFalse", "0" },
	});

	CHECK(attrs.GetValueAsBool("wordTrue"));
	CHECK(attrs.GetValueAsBool("digitTrue"));
	CHECK_FALSE(attrs.GetValueAsBool("wordFalse"));
	CHECK_FALSE(attrs.GetValueAsBool("digitFalse"));
}

TEST_CASE("XmlAttributes GetValueAsBool returns the default for anything else",
	"[xml_handler][attributes]")
{
	// Both the missing case and the unrecognised case fall through to the default, so a
	// typo such as TRUE or yes silently takes whatever the caller asked for.
	auto attrs = MakeAttributes({ { "shouted", "TRUE" }, { "worded", "yes" } });

	CHECK_FALSE(attrs.GetValueAsBool("shouted"));
	CHECK(attrs.GetValueAsBool("shouted", true));

	CHECK_FALSE(attrs.GetValueAsBool("worded"));
	CHECK(attrs.GetValueAsBool("worded", true));

	CHECK_FALSE(attrs.GetValueAsBool("missing"));
	CHECK(attrs.GetValueAsBool("missing", true));
}

TEST_CASE("XmlAttributes GetValueAsInt parses and falls back", "[xml_handler][attributes]")
{
	auto attrs = MakeAttributes({
		{ "positive", "42" },
		{ "negative", "-7" },
		{ "padded", "  13" },
		{ "trailing", "12abc" },
	});

	CHECK(attrs.GetValueAsInt("positive") == 42);
	CHECK(attrs.GetValueAsInt("negative") == -7);
	CHECK(attrs.GetValueAsInt("padded") == 13);

	// Extraction stops at the first character that cannot be part of the number and does
	// not fail, so trailing junk is silently dropped rather than rejected.
	CHECK(attrs.GetValueAsInt("trailing") == 12);

	CHECK(attrs.GetValueAsInt("missing") == 0);
	CHECK(attrs.GetValueAsInt("missing", 99) == 99);
}

TEST_CASE("XmlAttributes GetValueAsFloat parses and falls back", "[xml_handler][attributes]")
{
	auto attrs = MakeAttributes({
		{ "half", "0.5" },
		{ "negative", "-1.25" },
		{ "integral", "3" },
	});

	CHECK(attrs.GetValueAsFloat("half") == Approx(0.5f));
	CHECK(attrs.GetValueAsFloat("negative") == Approx(-1.25f));
	CHECK(attrs.GetValueAsFloat("integral") == Approx(3.0f));

	CHECK(attrs.GetValueAsFloat("missing") == Approx(0.0f));
	CHECK(attrs.GetValueAsFloat("missing", 2.5f) == Approx(2.5f));
}

// NOTE: GetValueAsInt/GetValueAsFloat ASSERT on input that fails to parse at all (e.g.
// "abc"), so that path aborts a debug build rather than returning the default. It is
// deliberately not exercised here; see the note in the suite's CMakeLists.
