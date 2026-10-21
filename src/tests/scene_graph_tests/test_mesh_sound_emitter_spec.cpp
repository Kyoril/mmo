// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/mesh_sound_emitter_spec.h"

using namespace mmo;

TEST_CASE("Mesh sound emitter specs parse with and without optional fields", "[mesh_sounds]")
{
	std::vector<MeshSoundEmitter> emitters;
	String error;
	REQUIRE(ParseMeshSoundEmitterSpec("300:0,1.25,-0.5; 301:2,0,0:0.4 ;302:0,0,0:1:15", emitters, error));

	REQUIRE(emitters.size() == 3);
	CHECK(emitters[0].soundEntryId == 300);
	CHECK(emitters[0].offset.y == Approx(1.25f));
	CHECK(emitters[0].offset.z == Approx(-0.5f));
	CHECK(emitters[0].volume == Approx(1.0f));
	CHECK(emitters[0].maxDistance == Approx(0.0f));
	CHECK(emitters[1].volume == Approx(0.4f));
	CHECK(emitters[2].maxDistance == Approx(15.0f));
}

TEST_CASE("An empty mesh sound emitter spec clears the emitters", "[mesh_sounds]")
{
	std::vector<MeshSoundEmitter> emitters(2);
	String error;
	CHECK(ParseMeshSoundEmitterSpec("  ", emitters, error));
	CHECK(emitters.empty());
}

TEST_CASE("Malformed mesh sound emitter specs are rejected and leave the output alone", "[mesh_sounds]")
{
	const std::vector<String> broken = {
		"abc:0,0,0", "0:0,0,0", "5:0,0", "5:0,0,x", "5:0,0,0:-1", "5:0,0,0:1:2:3", "5"
	};

	for (const String& spec : broken)
	{
		std::vector<MeshSoundEmitter> emitters(1);
		String error;
		INFO(spec);
		CHECK_FALSE(ParseMeshSoundEmitterSpec(spec, emitters, error));
		CHECK_FALSE(error.empty());
		CHECK(emitters.size() == 1);
	}
}

TEST_CASE("Formatted mesh sound emitter specs parse back", "[mesh_sounds]")
{
	MeshSoundEmitter a;
	a.soundEntryId = 7;
	a.offset = Vector3(0.5f, 1.0f, -2.0f);
	a.volume = 0.75f;
	a.maxDistance = 12.0f;

	std::vector<MeshSoundEmitter> parsed;
	String error;
	REQUIRE(ParseMeshSoundEmitterSpec(FormatMeshSoundEmitterSpec({ a, a }), parsed, error));
	REQUIRE(parsed.size() == 2);
	CHECK(parsed[1].offset.z == Approx(-2.0f));
	CHECK(parsed[1].volume == Approx(0.75f));
	CHECK(parsed[1].maxDistance == Approx(12.0f));
}
