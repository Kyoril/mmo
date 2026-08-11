// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "virtual_dir/path.h"

using namespace mmo;
using namespace mmo::virtual_dir;

TEST_CASE("appendPath inserts exactly one separator", "[virtual_dir][path]")
{
	Path left = "assets";
	appendPath(left, "models");
	CHECK(left == "assets/models");
}

TEST_CASE("appendPath does not double a separator already present", "[virtual_dir][path]")
{
	SECTION("trailing separator on the left operand")
	{
		Path left = "assets/";
		appendPath(left, "models");
		CHECK(left == "assets/models");
	}

	SECTION("leading separator on the right operand")
	{
		Path left = "assets";
		appendPath(left, "/models");
		CHECK(left == "assets/models");
	}

	SECTION("separators on both sides collapse to one")
	{
		Path left = "assets/";
		appendPath(left, "/models");
		CHECK(left == "assets/models");
	}
}

TEST_CASE("appendPath leaves an empty operand alone", "[virtual_dir][path]")
{
	// No separator is invented for an empty side, so appending to nothing yields the right
	// operand verbatim rather than a path rooted at "/".
	SECTION("empty left")
	{
		Path left;
		appendPath(left, "models");
		CHECK(left == "models");
	}

	SECTION("empty right")
	{
		Path left = "assets";
		appendPath(left, "");
		CHECK(left == "assets");
	}

	SECTION("both empty")
	{
		Path left;
		appendPath(left, "");
		CHECK(left.empty());
	}
}

TEST_CASE("joinPaths returns what appendPath would have produced", "[virtual_dir][path]")
{
	CHECK(joinPaths("assets", "models") == "assets/models");
	CHECK(joinPaths("assets/", "/models") == "assets/models");

	// The left operand is taken by value, so the caller's path is untouched.
	const Path left = "assets/";
	const Path joined = joinPaths(left, "models");
	CHECK(left == "assets/");
	CHECK(joined == "assets/models");
}

TEST_CASE("splitLeaf separates the last component, keeping the separator on the head",
	"[virtual_dir][path]")
{
	const auto split = splitLeaf("assets/models/tree.hmsh");
	CHECK(split.first == "assets/models/");
	CHECK(split.second == "tree.hmsh");
}

TEST_CASE("splitLeaf yields an empty head when there is no separator", "[virtual_dir][path]")
{
	const auto split = splitLeaf("tree.hmsh");
	CHECK(split.first.empty());
	CHECK(split.second == "tree.hmsh");
}

TEST_CASE("splitLeaf yields an empty leaf for a path ending in a separator",
	"[virtual_dir][path]")
{
	const auto split = splitLeaf("assets/");
	CHECK(split.first == "assets/");
	CHECK(split.second.empty());
}

TEST_CASE("splitLeaf handles the empty path", "[virtual_dir][path]")
{
	const auto split = splitLeaf("");
	CHECK(split.first.empty());
	CHECK(split.second.empty());
}

TEST_CASE("splitRoot separates the first component and drops the separator",
	"[virtual_dir][path]")
{
	const auto split = splitRoot("assets/models/tree.hmsh");
	CHECK(split.first == "assets");
	CHECK(split.second == "models/tree.hmsh");
}

TEST_CASE("splitRoot yields an empty remainder when there is no separator",
	"[virtual_dir][path]")
{
	const auto split = splitRoot("assets");
	CHECK(split.first == "assets");
	CHECK(split.second.empty());
}

TEST_CASE("splitRoot yields an empty root for an absolute path", "[virtual_dir][path]")
{
	const auto split = splitRoot("/assets/models");
	CHECK(split.first.empty());
	CHECK(split.second == "assets/models");
}

TEST_CASE("splitRoot consumes only the first of a doubled separator", "[virtual_dir][path]")
{
	// Only one separator is skipped, so the extra one stays at the front of the remainder.
	const auto split = splitRoot("assets//models");
	CHECK(split.first == "assets");
	CHECK(split.second == "/models");
}

TEST_CASE("splitRoot handles the empty path", "[virtual_dir][path]")
{
	const auto split = splitRoot("");
	CHECK(split.first.empty());
	CHECK(split.second.empty());
}
