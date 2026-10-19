// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"
#include "scene_graph/environment_state.h"

using namespace mmo;

TEST_CASE("Distance fog is off in an unauthored profile", "[environment][distance_fog]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(1);
	record.set_name("Test");

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	CHECK_FALSE(profile.atmosphere.HasDistanceFog());
	CHECK(profile.atmosphere.distanceFogSkyBand == Approx(0.2f));
}

TEST_CASE("Unauthored distance fog curve follows the sky horizon", "[environment][distance_fog]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(1);
	record.set_name("Test");
	auto* key = record.mutable_sky_horizon()->add_key();
	key->set_time(0.0f);
	key->set_r(0.25f);
	key->set_g(0.5f);
	key->set_b(0.75f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	const EnvironmentState state = EvaluateEnvironment(profile, 0.5f);
	CHECK(state.timeOfDay.distanceFogColor[0] == Approx(0.25f));
	CHECK(state.timeOfDay.distanceFogColor[1] == Approx(0.5f));
	CHECK(state.timeOfDay.distanceFogColor[2] == Approx(0.75f));
	CHECK(state.timeOfDay.distanceFogOpacity == Approx(1.0f));
}

TEST_CASE("Authored distance fog loads and evaluates", "[environment][distance_fog]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(1);
	record.set_name("Test");
	record.set_distance_fog_start(150.0f);
	record.set_distance_fog_end(900.0f);
	record.set_distance_fog_sky_band(0.3f);
	auto* key = record.mutable_distance_fog()->add_key();
	key->set_time(0.0f);
	key->set_r(0.6f);
	key->set_g(0.62f);
	key->set_b(0.65f);
	key->set_a(0.8f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	CHECK(profile.atmosphere.HasDistanceFog());

	const EnvironmentState state = EvaluateEnvironment(profile, 0.3f);
	CHECK(state.atmosphere.distanceFogStart == Approx(150.0f));
	CHECK(state.atmosphere.distanceFogEnd == Approx(900.0f));
	CHECK(state.atmosphere.distanceFogSkyBand == Approx(0.3f));
	CHECK(state.timeOfDay.distanceFogColor[0] == Approx(0.6f));
	CHECK(state.timeOfDay.distanceFogOpacity == Approx(0.8f));
}

TEST_CASE("Blending out of a distance-fogged profile pushes the fog away", "[environment][distance_fog]")
{
	EnvironmentState fogged;
	fogged.atmosphere.SetDistanceFogStart(200.0f);
	fogged.atmosphere.SetDistanceFogEnd(800.0f);
	const EnvironmentState clear;

	CHECK(LerpEnvironment(fogged, clear, 0.0f).atmosphere.distanceFogStart == Approx(200.0f));
	CHECK(LerpEnvironment(fogged, clear, 0.0f).atmosphere.distanceFogEnd == Approx(800.0f));

	// Halfway out the fog has moved far beyond any view distance instead of popping or collapsing.
	const EnvironmentState half = LerpEnvironment(fogged, clear, 0.5f);
	CHECK(half.atmosphere.HasDistanceFog());
	CHECK(half.atmosphere.distanceFogStart > 10000.0f);

	// Two profiles without distance fog blend to one without distance fog.
	CHECK_FALSE(LerpEnvironment(clear, clear, 0.5f).atmosphere.HasDistanceFog());

	// Two fogged profiles blend their ranges directly.
	EnvironmentState other;
	other.atmosphere.SetDistanceFogStart(400.0f);
	other.atmosphere.SetDistanceFogEnd(1200.0f);
	const EnvironmentState mixed = LerpEnvironment(fogged, other, 0.5f);
	CHECK(mixed.atmosphere.distanceFogStart == Approx(300.0f));
	CHECK(mixed.atmosphere.distanceFogEnd == Approx(1000.0f));
}
