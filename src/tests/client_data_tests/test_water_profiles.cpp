// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "shared/client_data/proto_client/water_profiles.pb.h"
#include "terrain/constants.h"

using namespace mmo;

TEST_CASE("WaterProfile_Ids_Match_WaterType_Enum", "[water_profiles]")
{
	// A water profile's id IS the terrain liquid type. If the enum is ever reordered without
	// the data being renumbered, every liquid silently renders with another liquid's material
	// and underwater fog, with nothing in any log to point at. Pin the mapping here.
	CHECK(static_cast<uint32>(terrain::WaterType::None) == 0u);
	CHECK(static_cast<uint32>(terrain::WaterType::Water) == 1u);
	CHECK(static_cast<uint32>(terrain::WaterType::Ocean) == 2u);
	CHECK(static_cast<uint32>(terrain::WaterType::Lava) == 3u);
	CHECK(static_cast<uint32>(terrain::WaterType::Slime) == 4u);
}

TEST_CASE("WaterProfile_Round_Trips_Through_Serialization", "[water_profiles]")
{
	proto_client::WaterProfiles profiles;

	proto_client::WaterProfile* ocean = profiles.add_entry();
	ocean->set_id(static_cast<uint32>(terrain::WaterType::Ocean));
	ocean->set_name("Ocean");
	ocean->set_surface_material("Worlds/Water_Ocean.hmat");
	ocean->set_fog_color(0xFF1E4D5Au);
	ocean->set_fog_density(0.035f);
	ocean->set_absorption_color(0xFF2E6B78u);
	ocean->set_caustics_strength(0.6f);
	ocean->set_caustics_texture("Textures/Caustics_01.htex");
	ocean->set_audio_lowpass_hz(900.0f);
	ocean->set_distortion_strength(0.4f);

	std::string bytes;
	REQUIRE(profiles.SerializeToString(&bytes));

	proto_client::WaterProfiles parsed;
	REQUIRE(parsed.ParseFromString(bytes));
	REQUIRE(parsed.entry_size() == 1);

	const proto_client::WaterProfile& got = parsed.entry(0);
	CHECK(got.id() == static_cast<uint32>(terrain::WaterType::Ocean));
	CHECK(got.name() == "Ocean");
	CHECK(got.surface_material() == "Worlds/Water_Ocean.hmat");
	CHECK(got.fog_color() == 0xFF1E4D5Au);
	CHECK(got.fog_density() == Approx(0.035f));
	CHECK(got.absorption_color() == 0xFF2E6B78u);
	CHECK(got.caustics_strength() == Approx(0.6f));
	CHECK(got.caustics_texture() == "Textures/Caustics_01.htex");
	CHECK(got.audio_lowpass_hz() == Approx(900.0f));
	CHECK(got.distortion_strength() == Approx(0.4f));
}

TEST_CASE("WaterProfile_Optional_Fields_Report_Absence", "[water_profiles]")
{
	// A profile that leaves caustics unset must be distinguishable from one that sets them to
	// zero, so a consumer can fall back to a sensible default instead of silently disabling
	// the effect for every liquid that has not been fully authored yet.
	proto_client::WaterProfile bare;
	bare.set_id(static_cast<uint32>(terrain::WaterType::Water));
	bare.set_name("Water");

	CHECK_FALSE(bare.has_caustics_strength());
	CHECK_FALSE(bare.has_surface_material());
	CHECK_FALSE(bare.has_audio_lowpass_hz());

	bare.set_caustics_strength(0.0f);
	CHECK(bare.has_caustics_strength());
	CHECK(bare.caustics_strength() == Approx(0.0f));
}

TEST_CASE("WaterProfile_Required_Fields_Are_Enforced", "[water_profiles]")
{
	// id and name are required, so a half-authored entry is detectable before it reaches
	// ClientDB as an entry that resolves to nothing.
	//
	// Check IsInitialized rather than the return of SerializeToString: protobuf treats a
	// missing required field as a hard CHECK failure inside serialization, not a false return,
	// so a caller that only tests the return value takes the process down instead of
	// reporting a bad entry.
	proto_client::WaterProfile incomplete;
	incomplete.set_name("Nameless");
	CHECK_FALSE(incomplete.IsInitialized());

	incomplete.set_id(static_cast<uint32>(terrain::WaterType::Ocean));
	CHECK(incomplete.IsInitialized());

	std::string bytes;
	CHECK(incomplete.SerializeToString(&bytes));
}
