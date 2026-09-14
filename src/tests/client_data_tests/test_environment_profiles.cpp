// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "shared/client_data/proto_client/environment_profiles.pb.h"
#include "shared/client_data/proto_client/maps.pb.h"
#include "shared/client_data/proto_client/zones.pb.h"
#include "scene_graph/atmosphere_settings.h"
#include "deferred_shading/bloom_settings.h"
#include "deferred_shading/tonemap_settings.h"

using namespace mmo;

TEST_CASE("EnvironmentProfile_Defaults_Match_Engine_Defaults", "[environment_profiles]")
{
	// An unauthored profile must render exactly like the engine defaults, so the proto defaults
	// and the runtime structs can never drift apart silently.
	const proto_client::EnvironmentProfile profile;
	const AtmosphereParameters atmosphere;
	const BloomSettings bloom;
	const TonemapSettings tonemap;

	CHECK(profile.fog_density() == Approx(atmosphere.density));
	CHECK(profile.fog_height_falloff() == Approx(atmosphere.heightFalloff));
	CHECK(profile.fog_base_height() == Approx(atmosphere.baseHeight));
	CHECK(profile.fog_anisotropy() == Approx(atmosphere.anisotropy));
	CHECK(profile.shaft_strength() == Approx(atmosphere.shaftStrength));
	CHECK(profile.exposure() == Approx(tonemap.exposure));
	CHECK(profile.bloom_intensity() == Approx(bloom.intensity));
	CHECK(profile.bloom_threshold() == Approx(bloom.threshold));
	CHECK(profile.transition_seconds() == Approx(3.0f));
}

TEST_CASE("EnvironmentProfile_Round_Trips_Curves_And_Values", "[environment_profiles]")
{
	proto_client::EnvironmentProfiles profiles;
	proto_client::EnvironmentProfile* swamp = profiles.add_entry();
	swamp->set_id(7);
	swamp->set_name("Swamp");
	swamp->set_fog_density(0.02f);
	swamp->set_transition_seconds(5.0f);

	proto_client::ColorCurveKey* key = swamp->mutable_fog()->add_key();
	key->set_time(0.75f);
	key->set_r(0.2f);
	key->set_g(0.5f);
	key->set_b(0.1f);
	key->set_a(2.0f);
	key->set_tangent_mode(1);
	for (int i = 0; i < 4; ++i)
	{
		key->add_in_tangent(0.5f);
		key->add_out_tangent(-0.5f);
	}

	std::string bytes;
	REQUIRE(profiles.SerializeToString(&bytes));

	proto_client::EnvironmentProfiles parsed;
	REQUIRE(parsed.ParseFromString(bytes));
	REQUIRE(parsed.entry_size() == 1);

	const proto_client::EnvironmentProfile& got = parsed.entry(0);
	CHECK(got.id() == 7u);
	CHECK(got.name() == "Swamp");
	CHECK(got.fog_density() == Approx(0.02f));
	CHECK(got.transition_seconds() == Approx(5.0f));
	CHECK_FALSE(got.has_sky_horizon());
	REQUIRE(got.fog().key_size() == 1);
	CHECK(got.fog().key(0).time() == Approx(0.75f));
	CHECK(got.fog().key(0).a() == Approx(2.0f));
	CHECK(got.fog().key(0).tangent_mode() == 1u);
	CHECK(got.fog().key(0).in_tangent_size() == 4);
	CHECK(got.fog().key(0).out_tangent(3) == Approx(-0.5f));
}

TEST_CASE("Zone_And_Map_Environment_Profile_Default_To_Zero", "[environment_profiles]")
{
	// 0 means "inherit" on a zone and "built-in Default" on a map.
	proto_client::ZoneEntry zone;
	proto_client::MapEntry map;
	CHECK(zone.environment_profile() == 0u);
	CHECK(map.environment_profile() == 0u);

	zone.set_environment_profile(3);
	map.set_environment_profile(4);
	CHECK(zone.environment_profile() == 3u);
	CHECK(map.environment_profile() == 4u);
}

TEST_CASE("ColorCurveKey_Alpha_Defaults_To_One", "[environment_profiles]")
{
	const proto_client::ColorCurveKey key;
	CHECK(key.a() == Approx(1.0f));
	CHECK(key.tangent_mode() == 0u);
}
