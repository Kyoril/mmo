// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"

using namespace mmo;

namespace
{
	void CheckColor(const Vector4& actual, const Vector4& expected)
	{
		CHECK(actual.x == Approx(expected.x).margin(1e-4));
		CHECK(actual.y == Approx(expected.y).margin(1e-4));
		CHECK(actual.z == Approx(expected.z).margin(1e-4));
		CHECK(actual.w == Approx(expected.w).margin(1e-4));
	}

	proto_client::ColorCurveKey* AddKey(proto_client::ColorCurveData& data, const float time, const float r, const float g, const float b, const float a)
	{
		proto_client::ColorCurveKey* key = data.add_key();
		key->set_time(time);
		key->set_r(r);
		key->set_g(g);
		key->set_b(b);
		key->set_a(a);
		return key;
	}
}

TEST_CASE("Default environment reproduces the legacy sky curves at their keys", "[environment]")
{
	const EnvironmentProfile profile = EnvironmentProfile::MakeDefault();

	// Seeded from the shipped Models/*.hccv files.
	CheckColor(profile.skyHorizon.Evaluate(0.2975f), Vector4(0.9964f, 0.6f, 0.2f, 1.0f));
	CheckColor(profile.skyZenith.Evaluate(0.4321f), Vector4(0.0431f, 0.0863f, 0.2039f, 1.0f));
	CheckColor(profile.clouds.Evaluate(0.5003f), Vector4(0.9479f, 0.9479f, 0.9479f, 1.0f));

	// Seeded from the SkyComponent fallback keys (no files ship for these two).
	CheckColor(profile.sunScatter.Evaluate(0.23f), Vector4(1.0f, 0.42f, 0.18f, 0.8f));

	// Formerly hardcoded light colours, now constant curves.
	CheckColor(profile.sun.Evaluate(0.37f), Vector4(1.0f, 0.95f, 0.9f, 1.0f));
	CheckColor(profile.moon.Evaluate(0.91f), Vector4(0.3f, 0.4f, 0.65f, 0.12f));

	const AtmosphereParameters atmosphere;
	CHECK(profile.atmosphere.density == Approx(atmosphere.density));
	CHECK(profile.exposure == Approx(1.0f));
	CHECK(profile.bloomIntensity == Approx(0.08f));
	CHECK(profile.bloomThreshold == Approx(0.8f));
	CHECK(profile.transitionSeconds == Approx(3.0f));
}

TEST_CASE("Default environment keeps its tuned fog and ambient keys", "[environment]")
{
	// These two curves deliberately left their legacy values behind: the .hccv ambient read murky
	// blue-green in daylight shadow, and the fallback fog keys washed dawn and dusk to flat sepia.
	const EnvironmentProfile profile = EnvironmentProfile::MakeDefault();

	CheckColor(profile.ambient.Evaluate(0.5003f), Vector4(0.036f, 0.0455f, 0.061f, 0.999f));

	// Midday fog: daylight tint at 0.9x density.
	CheckColor(profile.fog.Evaluate(0.5f), Vector4(0.55f, 0.7f, 0.9f, 0.9f));

	// Dawn and dusk stay warm but well below the sky's brightness, at a mild density multiplier.
	CheckColor(profile.fog.Evaluate(0.27f), Vector4(0.6f, 0.45f, 0.36f, 1.15f));
	CheckColor(profile.fog.Evaluate(0.73f), Vector4(0.6f, 0.43f, 0.34f, 1.15f));
}

TEST_CASE("Default environment is shared and never null", "[environment]")
{
	const auto& a = EnvironmentProfile::GetDefault();
	const auto& b = EnvironmentProfile::GetDefault();
	REQUIRE(a != nullptr);
	CHECK(a.get() == b.get());
}

TEST_CASE("Loading a profile keeps authored curves and fills empty ones from Default", "[environment]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(2);
	record.set_name("Swamp");
	AddKey(*record.mutable_fog(), 0.0f, 0.1f, 0.4f, 0.1f, 3.0f);
	AddKey(*record.mutable_fog(), 1.0f, 0.1f, 0.4f, 0.1f, 3.0f);
	record.set_fog_density(0.02f);
	record.set_exposure(1.5f);
	record.set_transition_seconds(6.0f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	const EnvironmentProfile defaults = EnvironmentProfile::MakeDefault();

	CheckColor(profile.fog.Evaluate(0.5f), Vector4(0.1f, 0.4f, 0.1f, 3.0f));
	CheckColor(profile.skyHorizon.Evaluate(0.2975f), defaults.skyHorizon.Evaluate(0.2975f));
	CHECK(profile.atmosphere.density == Approx(0.02f));
	CHECK(profile.atmosphere.heightFalloff == Approx(0.05f));
	CHECK(profile.exposure == Approx(1.5f));
	CHECK(profile.transitionSeconds == Approx(6.0f));
}

TEST_CASE("Loading a profile clamps out-of-range fixed values", "[environment]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(3);
	record.set_name("Broken");
	record.set_fog_density(-1.0f);
	record.set_fog_anisotropy(2.0f);
	record.set_exposure(100.0f);
	record.set_bloom_intensity(5.0f);
	record.set_transition_seconds(-2.0f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	CHECK(profile.atmosphere.density == Approx(0.0f));
	CHECK(profile.atmosphere.anisotropy == Approx(0.95f));
	CHECK(profile.exposure == Approx(8.0f));
	CHECK(profile.bloomIntensity == Approx(1.0f));
	CHECK(profile.transitionSeconds == Approx(0.0f));
}

TEST_CASE("User tangents survive a store and load round trip, malformed ones fall back to auto", "[environment]")
{
	ColorCurve curve = MakeEnvironmentCurve({ { 0.0f, Vector4(0.0f, 0.0f, 0.0f, 1.0f) }, { 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f) } });
	ColorKey userKey = curve.GetKey(0);
	userKey.inTangent = Vector4(2.0f, 2.0f, 2.0f, 0.0f);
	userKey.outTangent = Vector4(2.0f, 2.0f, 2.0f, 0.0f);
	userKey.tangentMode = 1;
	curve.UpdateKey(0, userKey);

	proto_client::ColorCurveData data;
	StoreColorCurve(curve, data);
	REQUIRE(data.key_size() == 2);
	CHECK(data.key(0).in_tangent_size() == 4);
	CHECK(data.key(1).in_tangent_size() == 0);

	ColorCurve loaded;
	REQUIRE(LoadColorCurve(data, loaded));
	CHECK(loaded.GetKey(0).tangentMode == 1);
	CHECK(loaded.GetKey(0).outTangent.x == Approx(2.0f));

	// Three tangent values are malformed: the key must load with auto tangents.
	data.mutable_key(0)->mutable_in_tangent()->RemoveLast();
	ColorCurve fallback;
	REQUIRE(LoadColorCurve(data, fallback));
	CHECK(fallback.GetKey(0).tangentMode == 0);
}

TEST_CASE("An empty curve record leaves the target curve untouched", "[environment]")
{
	const proto_client::ColorCurveData empty;
	ColorCurve curve = MakeEnvironmentCurve({ { 0.0f, Vector4(0.5f, 0.5f, 0.5f, 1.0f) } });
	CHECK_FALSE(LoadColorCurve(empty, curve));
	CheckColor(curve.Evaluate(0.3f), Vector4(0.5f, 0.5f, 0.5f, 1.0f));
}

TEST_CASE("Profile id resolves zone, parent chain, map default, then Default", "[environment]")
{
	proto_client::ZoneManager zones;
	proto_client::MapManager maps;

	proto_client::MapEntry* map = maps.add(1);
	map->set_name("Map");
	map->set_directory("Map");
	map->set_environment_profile(40);

	proto_client::ZoneEntry* root = zones.add(10);
	root->set_name("Root");
	root->set_environment_profile(20);

	proto_client::ZoneEntry* child = zones.add(11);
	child->set_name("Child");
	child->set_parentzone(10);

	proto_client::ZoneEntry* own = zones.add(12);
	own->set_name("Own");
	own->set_parentzone(10);
	own->set_environment_profile(30);

	proto_client::ZoneEntry* orphan = zones.add(13);
	orphan->set_name("Orphan");

	CHECK(ResolveEnvironmentProfileId(zones, maps, 12, 1) == 30u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 11, 1) == 20u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 13, 1) == 40u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 0, 1) == 40u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 999, 1) == 40u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 13, 2) == 0u);
}

TEST_CASE("Profile id resolution survives a parent cycle", "[environment]")
{
	proto_client::ZoneManager zones;
	proto_client::MapManager maps;

	proto_client::ZoneEntry* a = zones.add(1);
	a->set_name("A");
	a->set_parentzone(2);
	proto_client::ZoneEntry* b = zones.add(2);
	b->set_name("B");
	b->set_parentzone(1);

	CHECK(ResolveEnvironmentProfileId(zones, maps, 1, 0) == 0u);
}

TEST_CASE("Profile cache maps unknown ids and id 0 to the shared Default", "[environment]")
{
	proto_client::EnvironmentProfileManager profiles;
	proto_client::EnvironmentProfile* dusk = profiles.add(5);
	dusk->set_name("Dusk");
	dusk->set_exposure(0.5f);

	EnvironmentProfileCache<proto_client::EnvironmentProfileManager> cache(profiles);
	CHECK(cache.Get(0).get() == EnvironmentProfile::GetDefault().get());
	CHECK(cache.Get(77).get() == EnvironmentProfile::GetDefault().get());

	const auto first = cache.Get(5);
	REQUIRE(first != nullptr);
	CHECK(first->exposure == Approx(0.5f));
	CHECK(cache.Get(5).get() == first.get());

	// Clear drops cached conversions so edited records are converted again.
	dusk->set_exposure(2.0f);
	cache.Clear();
	CHECK(cache.Get(5)->exposure == Approx(2.0f));
}
