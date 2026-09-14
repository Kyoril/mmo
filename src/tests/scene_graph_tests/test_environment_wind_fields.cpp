// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"
#include "scene_graph/environment_state.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Wind proto defaults equal the runtime profile defaults", "[wind]")
{
	const proto_client::EnvironmentProfile record;
	const EnvironmentProfile profile;

	CHECK(record.wind_direction() == Approx(profile.windDirectionDegrees));
	CHECK(record.wind_speed() == Approx(profile.windSpeed));
	CHECK(record.wind_gustiness() == Approx(profile.windGustiness));
	CHECK(record.fog_noise_amount() == Approx(profile.fogNoiseAmount));
	CHECK(record.fog_noise_size() == Approx(profile.fogNoiseSize));

	CHECK(profile.windDirectionDegrees == Approx(45.0f));
	CHECK(profile.windSpeed == Approx(3.0f));
	CHECK(profile.windGustiness == Approx(0.3f));
	CHECK(profile.fogNoiseAmount == Approx(0.5f));
	CHECK(profile.fogNoiseSize == Approx(60.0f));
}

TEST_CASE("Loading wind fields wraps the direction and clamps the rest", "[wind]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(1);
	record.set_name("Storm");
	record.set_wind_direction(-90.0f);
	record.set_wind_speed(99.0f);
	record.set_wind_gustiness(-1.0f);
	record.set_fog_noise_amount(3.0f);
	record.set_fog_noise_size(1.0f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	CHECK(profile.windDirectionDegrees == Approx(270.0f));
	CHECK(profile.windSpeed == Approx(30.0f));
	CHECK(profile.windGustiness == Approx(0.0f));
	CHECK(profile.fogNoiseAmount == Approx(1.0f));
	CHECK(profile.fogNoiseSize == Approx(5.0f));

	record.set_wind_direction(725.0f);
	record.set_fog_noise_size(9000.0f);
	const EnvironmentProfile wrapped = LoadEnvironmentProfile(record);
	CHECK(wrapped.windDirectionDegrees == Approx(5.0f));
	CHECK(wrapped.fogNoiseSize == Approx(500.0f));
}

TEST_CASE("Wind direction degrees map clockwise from +Z", "[wind]")
{
	const Vector3 north = WindDirectionFromDegrees(0.0f);
	CHECK(north.x == Approx(0.0f).margin(1e-5));
	CHECK(north.z == Approx(1.0f));

	const Vector3 east = WindDirectionFromDegrees(90.0f);
	CHECK(east.x == Approx(1.0f));
	CHECK(east.z == Approx(0.0f).margin(1e-5));
	CHECK(east.y == Approx(0.0f));
}

TEST_CASE("Evaluating a profile copies the wind values", "[wind]")
{
	EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
	profile.windDirectionDegrees = 180.0f;
	profile.windSpeed = 7.0f;
	profile.windGustiness = 0.8f;
	profile.fogNoiseAmount = 0.2f;
	profile.fogNoiseSize = 120.0f;

	const EnvironmentState state = EvaluateEnvironment(profile, 0.5f);
	CHECK(state.windDirection.z == Approx(-1.0f));
	CHECK(state.windSpeed == Approx(7.0f));
	CHECK(state.windGustiness == Approx(0.8f));
	CHECK(state.fogNoiseAmount == Approx(0.2f));
	CHECK(state.fogNoiseSize == Approx(120.0f));
}

TEST_CASE("Wind direction blends along the shortest arc", "[wind]")
{
	EnvironmentState a;
	EnvironmentState b;
	a.windDirection = WindDirectionFromDegrees(350.0f);
	b.windDirection = WindDirectionFromDegrees(10.0f);
	a.windSpeed = 2.0f;
	b.windSpeed = 6.0f;

	const EnvironmentState half = LerpEnvironment(a, b, 0.5f);
	CHECK(half.windDirection.z == Approx(1.0f));
	CHECK(half.windDirection.x == Approx(0.0f).margin(1e-4));
	CHECK(half.windSpeed == Approx(4.0f));

	const float length = std::sqrt(half.windDirection.x * half.windDirection.x + half.windDirection.z * half.windDirection.z);
	CHECK(length == Approx(1.0f));
}

TEST_CASE("Opposite winds fall back to the incoming direction", "[wind]")
{
	EnvironmentState a;
	EnvironmentState b;
	a.windDirection = WindDirectionFromDegrees(0.0f);
	b.windDirection = WindDirectionFromDegrees(180.0f);

	const EnvironmentState half = LerpEnvironment(a, b, 0.5f);
	CHECK(half.windDirection.z == Approx(-1.0f));
}
