// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/glue_scene_definition.h"

using namespace mmo;

namespace
{
	String makeJson(int spotCount, bool withCamera)
	{
		const String camera = withCamera ? R"(, "camera": { "position": [1, 2, 3], "lookAt": [4, 5, 6] })" : "";

		String spots;
		for (int i = 0; i < spotCount; ++i)
		{
			if (i > 0)
			{
				spots += ",";
			}
			spots += R"({ "position": [)" + std::to_string(i) + R"(, 0, 0], "facing": 90.0, "pose": "pose)" + std::to_string(i) + R"(")" + camera + " }";
		}

		return R"({
			"world": "Alestia",
			"gatePages": [[31, 32]],
			"plazaPages": [[31, 33], [32, 33]],
			"environmentProfile": 3,
			"flightPath": [
				{ "position": [0, 0, 0], "lookAt": [0, 0, 1] },
				{ "position": [10, 0, 0], "lookAt": [10, 0, 1] }
			],
			"flightSeconds": 6.5,
			"front": { "position": [5, 0, 5], "facing": 180, "pose": "idle" },
			"spots": [)" + spots + R"(],
			"entrance": [1, 2, 3]
		})";
	}
}

TEST_CASE("A valid glue scene definition parses", "[glue_scene]")
{
	const auto def = ParseGlueSceneDefinition(makeJson(5, true));
	REQUIRE(def.has_value());
	CHECK(def->world == "Alestia");
	CHECK(def->gatePages.size() == 1);
	CHECK(def->plazaPages.size() == 2);
	CHECK(def->plazaPages[1].first == 32);
	CHECK(def->environmentProfile == 3);
	CHECK(def->flightPath.size() == 2);
	CHECK(def->flightSeconds == Approx(6.5f));
	CHECK(def->spots[4].pose == "pose4");
	CHECK(def->spots[4].position.x == Approx(4.0f));
	CHECK(def->spots[0].camera.lookAt.z == Approx(6.0f));
	CHECK(def->entrance.y == Approx(2.0f));
}

TEST_CASE("Spot cameras are optional", "[glue_scene]")
{
	const auto def = ParseGlueSceneDefinition(makeJson(5, false));
	REQUIRE(def.has_value());
	CHECK(def->front.pose == "idle");
}

TEST_CASE("A glue scene definition without exactly five spots is rejected", "[glue_scene]")
{
	CHECK_FALSE(ParseGlueSceneDefinition(makeJson(4, true)).has_value());
	CHECK_FALSE(ParseGlueSceneDefinition(makeJson(6, true)).has_value());
	CHECK_FALSE(ParseGlueSceneDefinition("not json").has_value());
	CHECK_FALSE(ParseGlueSceneDefinition("{}").has_value());
}
