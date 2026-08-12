// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/localization.h"

#include <string>
#include <vector>

using namespace mmo;

namespace
{
	// Minimal stand-in for a protobuf LocalizedString (the helpers are templated on any type
	// exposing locale()/value()/set_locale/set_value).
	struct MockLoc
	{
		uint32 m_locale = 0;
		std::string m_value;
		uint32 locale() const { return m_locale; }
		const std::string& value() const { return m_value; }
		void set_locale(const uint32 v) { m_locale = v; }
		void set_value(const std::string& v) { m_value = v; }
	};

	// Minimal stand-in for a protobuf RepeatedPtrField.
	struct MockRepeated
	{
		std::vector<MockLoc> items;
		auto begin() { return items.begin(); }
		auto end() { return items.end(); }
		auto begin() const { return items.begin(); }
		auto end() const { return items.end(); }
		MockLoc* Add() { items.emplace_back(); return &items.back(); }
	};
}

TEST_CASE("GetLocalizedString selection and fallback", "[localization]")
{
	const std::string base = "Sword of Truth";

	MockRepeated loc;
	{ auto* e = loc.Add(); e->set_locale(static_cast<uint32>(LocaleIndex::deDE)); e->set_value("Schwert der Wahrheit"); }
	{ auto* e = loc.Add(); e->set_locale(static_cast<uint32>(LocaleIndex::frFR)); e->set_value(""); }

	SECTION("enUS always returns the base English field")
	{
		REQUIRE(GetLocalizedString(base, loc, LocaleIndex::enUS) == base);
	}
	SECTION("Present override is returned")
	{
		REQUIRE(GetLocalizedString(base, loc, LocaleIndex::deDE) == "Schwert der Wahrheit");
	}
	SECTION("Present-but-empty override falls back to base")
	{
		REQUIRE(GetLocalizedString(base, loc, LocaleIndex::frFR) == base);
	}
	SECTION("Missing locale falls back to base")
	{
		REQUIRE(GetLocalizedString(base, loc, LocaleIndex::ruRU) == base);
	}
}

TEST_CASE("FindOrAddLoc adds and updates in place", "[localization]")
{
	const std::string base = "Sword of Truth";
	MockRepeated loc;

	SECTION("Adds a missing locale")
	{
		FindOrAddLoc(&loc, LocaleIndex::ruRU)->set_value("Mech Istiny");
		REQUIRE(GetLocalizedString(base, loc, LocaleIndex::ruRU) == "Mech Istiny");
		REQUIRE(loc.items.size() == 1);
	}

	SECTION("Updates existing without duplicating")
	{
		FindOrAddLoc(&loc, LocaleIndex::deDE)->set_value("A");
		FindOrAddLoc(&loc, LocaleIndex::deDE)->set_value("B");
		REQUIRE(loc.items.size() == 1);
		REQUIRE(GetLocalizedString(base, loc, LocaleIndex::deDE) == "B");
	}
}

TEST_CASE("Locale identifier conversions round-trip", "[localization]")
{
	SECTION("String to index")
	{
		REQUIRE(LocaleIndexFromString("deDE") == LocaleIndex::deDE);
		REQUIRE(LocaleIndexFromString("enGB") == LocaleIndex::enGB);
		REQUIRE(LocaleIndexFromString("xxYY") == LocaleIndex::enUS);	// unknown -> default
	}
	SECTION("Index to code")
	{
		REQUIRE(std::string(LocaleCode(LocaleIndex::enGB)) == "enGB");
		REQUIRE(std::string(LocaleCode(LocaleIndex::ruRU)) == "ruRU");
	}
	SECTION("FourCC round-trip")
	{
		REQUIRE(LocaleIndexFromFourCC(LocaleFourCC(LocaleIndex::deDE)) == LocaleIndex::deDE);
		REQUIRE(LocaleIndexFromFourCC(LocaleFourCC(LocaleIndex::frFR)) == LocaleIndex::frFR);
		REQUIRE(LocaleIndexFromFourCC(0x12345678) == LocaleIndex::enUS);	// unknown -> default
		// The deDE FourCC matches the value the client historically sent in the logon challenge.
		REQUIRE(LocaleFourCC(LocaleIndex::deDE) == 0x64654445u);
	}
	SECTION("Raw value clamping")
	{
		REQUIRE(LocaleIndexFromValue(static_cast<uint32>(LocaleIndex::ruRU)) == LocaleIndex::ruRU);
		REQUIRE(LocaleIndexFromValue(9999) == LocaleIndex::enUS);	// out of range -> default
	}
}
