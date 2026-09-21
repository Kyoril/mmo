// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"
#include "scene_graph/environment_state.h"

using namespace mmo;

TEST_CASE("Light scattering defaults to one", "[environment]")
{
	const EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
	CHECK(profile.lightScattering == Approx(1.0f));
	CHECK(EvaluateEnvironment(profile, 0.5f).lightScattering == Approx(1.0f));
}

TEST_CASE("Light scattering loads clamped from the profile record", "[environment]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(7);
	record.set_name("Test");
	CHECK(record.light_scattering() == Approx(1.0f));

	record.set_light_scattering(20.0f);
	CHECK(LoadEnvironmentProfile(record).lightScattering == Approx(8.0f));

	record.set_light_scattering(-1.0f);
	CHECK(LoadEnvironmentProfile(record).lightScattering == Approx(0.0f));

	record.set_light_scattering(2.5f);
	CHECK(LoadEnvironmentProfile(record).lightScattering == Approx(2.5f));
}

TEST_CASE("Light scattering blends linearly between profiles", "[environment]")
{
	EnvironmentState a;
	EnvironmentState b;
	a.lightScattering = 1.0f;
	b.lightScattering = 3.0f;
	CHECK(LerpEnvironment(a, b, 0.5f).lightScattering == Approx(2.0f));
}
