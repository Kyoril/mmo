// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "assets/asset_reference_scan.h"

#include <set>

using namespace mmo;

namespace
{
	/// Writes a string the way the asset formats do: a length prefix of the given width, then the characters.
	std::string LengthPrefixed(const std::string& value, const size_t prefixBytes)
	{
		std::string result;
		size_t length = value.size();
		for (size_t i = 0; i < prefixBytes; ++i)
		{
			result.push_back(static_cast<char>(length & 0xff));
			length >>= 8;
		}
		return result + value;
	}

	std::vector<String> Scan(const std::string& content, const std::set<String>& existing)
	{
		std::vector<String> references;
		ScanAssetReferences(content, [&existing](const String& name) { return existing.contains(name); }, references);
		return references;
	}
}

TEST_CASE("Asset references are found behind binary length prefixes", "[asset_reference_scan]")
{
	const std::string content = std::string("\x01\x00\x00\x00", 4) + LengthPrefixed("Models/Tree.hmsh", 2) + std::string("\xff\x10", 2) + LengthPrefixed("Textures/Bark.htex", 1);

	const auto references = Scan(content, { "Models/Tree.hmsh", "Textures/Bark.htex" });

	REQUIRE(references == std::vector<String>{ "Models/Tree.hmsh", "Textures/Bark.htex" });
}

TEST_CASE("A printable length prefix glued onto a name is dropped", "[asset_reference_scan]")
{
	// 65 characters long, so the uint8 length prefix is 'A' and reads as part of the path.
	const String name = "Models/" + std::string(53, 'x') + ".hmat";
	REQUIRE(name.size() == 65);

	const auto references = Scan(LengthPrefixed(name, 1), { name });

	REQUIRE(references == std::vector<String>{ name });
}

TEST_CASE("Two adjacent names are told apart", "[asset_reference_scan]")
{
	// The second name's printable prefix makes both read as one long run of path characters.
	const String first = "Models/A.hmsh";
	const String second = "Models/" + std::string(58, 'b') + ".hmsh";
	const auto references = Scan(LengthPrefixed(first, 1) + LengthPrefixed(second, 1), { first, second });

	REQUIRE(references == std::vector<String>{ first, second });
}

TEST_CASE("Names that do not exist are not reported", "[asset_reference_scan]")
{
	const auto references = Scan(LengthPrefixed("Models/Missing.hmsh", 2) + "random.htexture junk .hmi", {});

	REQUIRE(references.empty());
}

TEST_CASE("Backslashes are reported as forward slashes, and each name once", "[asset_reference_scan]")
{
	const auto references = Scan(LengthPrefixed("Textures\\Stone.htex", 2) + LengthPrefixed("Textures/Stone.htex", 2), { "Textures/Stone.htex" });

	REQUIRE(references == std::vector<String>{ "Textures/Stone.htex" });
}

TEST_CASE("Only formats that reference assets are worth scanning", "[asset_reference_scan]")
{
	CHECK(CanReferenceAssets("Worlds/Falwyn/Entities/12.wobj"));
	CHECK(CanReferenceAssets("Models/House.HWMO"));
	CHECK(CanReferenceAssets("Models/Rock.hmsh"));
	CHECK(CanReferenceAssets("Models/Rock.hmi"));
	CHECK(CanReferenceAssets("Models/Wolf/Wolf.char"));
	CHECK_FALSE(CanReferenceAssets("Textures/Rock.htex"));
	CHECK_FALSE(CanReferenceAssets("Sound/Bell.wav"));
	CHECK_FALSE(CanReferenceAssets("NoExtension"));
}
