// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/water_volume_system.h"

#include <optional>

using namespace mmo;

namespace
{
	/// Liquid type ids, matching terrain::WaterType without depending on the terrain library.
	constexpr uint32 TypeWater = 1;
	constexpr uint32 TypeOcean = 2;

	/// A coastline described in a few lines: water exists wherever x > 0, its surface sits at
	/// y = 0, and it is Ocean. Individual tests override the pieces they care about.
	class FakeWaterQuery final : public IWaterQuery
	{
	public:
		float surfaceHeight{ 0.0f };
		uint32 waterType{ TypeOcean };

		/// When false, no water exists anywhere regardless of position.
		bool waterExists{ true };

		[[nodiscard]] bool HasWaterAt(const float x, const float) const override
		{
			return waterExists && x > 0.0f;
		}

		[[nodiscard]] float GetWaterHeightAt(const float, const float) const override
		{
			return surfaceHeight;
		}

		[[nodiscard]] uint32 GetWaterTypeAt(const float x, const float z) const override
		{
			return HasWaterAt(x, z) ? waterType : 0u;
		}
	};

	WaterProfileValues MakeOceanProfile()
	{
		WaterProfileValues profile;
		profile.valid = true;
		profile.fogDensity = 0.035f;
		profile.fogColor[0] = 0.12f;
		profile.fogColor[1] = 0.30f;
		profile.fogColor[2] = 0.35f;
		profile.absorptionColor[0] = 0.45f;
		profile.absorptionColor[1] = 0.15f;
		profile.absorptionColor[2] = 0.09f;
		profile.causticsStrength = 0.6f;
		profile.distortionStrength = 0.4f;
		profile.audioLowPassHz = 900.0f;
		return profile;
	}

	/// Steps the system until its crossings settle, so a test can assert the steady state.
	void Settle(WaterVolumeSystem& system, const float cameraY, const float playerY,
		const float x = 10.0f)
	{
		for (int frame = 0; frame < 60; ++frame)
		{
			system.Update(x, cameraY, 0.0f, x, playerY, 0.0f, 1.0f / 60.0f);
		}
	}
}

TEST_CASE("WaterVolume_Camera_Above_Surface_Is_Dry", "[water_volume]")
{
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	system.Update(10.0f, 5.0f, 0.0f, 10.0f, 5.0f, 0.0f, 1.0f / 60.0f);

	CHECK_FALSE(system.GetState().active);
	CHECK(system.GetState().submersionDepth == Approx(0.0f));
	CHECK(system.GetState().transitionPhase == Approx(0.0f));
	CHECK_FALSE(system.IsPlayerSubmerged());
}

TEST_CASE("WaterVolume_Camera_Below_Surface_Is_Submerged", "[water_volume]")
{
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	system.Update(10.0f, -3.0f, 0.0f, 10.0f, -3.0f, 0.0f, 1.0f / 60.0f);

	CHECK(system.GetState().active);
	CHECK(system.GetState().submersionDepth == Approx(3.0f));
	CHECK(system.GetState().surfaceHeight == Approx(0.0f));
	CHECK(system.IsPlayerSubmerged());
	CHECK(system.GetCameraWaterType() == TypeOcean);
}

TEST_CASE("WaterVolume_Below_Sea_Level_On_Dry_Land_Is_Not_Submerged", "[water_volume]")
{
	// The bug the entire presence-versus-height distinction exists to prevent. Terrain water
	// heights are zero-initialised, so asking for a height where no water was ever painted
	// returns 0. A system keying off height alone would decide that every character standing in
	// a valley below sea level is swimming.
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	// x < 0 is the dry side of the coastline, and the position is well below y = 0.
	system.Update(-50.0f, -20.0f, 0.0f, -50.0f, -20.0f, 0.0f, 1.0f / 60.0f);

	CHECK_FALSE(system.GetState().active);
	CHECK_FALSE(system.IsPlayerSubmerged());
	CHECK(system.GetState().submersionDepth == Approx(0.0f));
	CHECK(system.GetCameraWaterType() == 0u);
}

TEST_CASE("WaterVolume_No_Water_Anywhere_Is_Never_Submerged", "[water_volume]")
{
	FakeWaterQuery query;
	query.waterExists = false;
	WaterVolumeSystem system(query);

	system.Update(10.0f, -100.0f, 0.0f, 10.0f, -100.0f, 0.0f, 1.0f / 60.0f);

	CHECK_FALSE(system.GetState().active);
	CHECK_FALSE(system.IsPlayerSubmerged());
}

TEST_CASE("WaterVolume_Camera_Under_While_Player_Dry", "[water_volume]")
{
	// The third-person camera has dipped below the surface while the character still stands on
	// the beach shelf. The screen effect follows the camera; the swim state does not.
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	system.Update(10.0f, -1.0f, 0.0f, 10.0f, 2.0f, 0.0f, 1.0f / 60.0f);

	CHECK(system.GetState().active);
	CHECK_FALSE(system.IsPlayerSubmerged());
}

TEST_CASE("WaterVolume_Player_Under_While_Camera_Dry", "[water_volume]")
{
	// The character is swimming with the camera held above the surface behind them. The screen
	// stays clear, but the world must still sound muffled.
	FakeWaterQuery query;
	WaterVolumeSystem system(query);
	system.SetProfileResolver([](uint32) { return MakeOceanProfile(); });

	Settle(system, 3.0f, -2.0f);

	CHECK_FALSE(system.GetState().active);
	CHECK(system.IsPlayerSubmerged());
	CHECK(system.GetAudioLowPassHz() == Approx(900.0f));
}

TEST_CASE("WaterVolume_Transition_Ramps_In_And_Out", "[water_volume]")
{
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	// Diving: the phase climbs from 0 rather than snapping to 1 on the first frame.
	system.Update(10.0f, -5.0f, 0.0f, 10.0f, -5.0f, 0.0f, 1.0f / 60.0f);
	const float firstFrame = system.GetState().transitionPhase;
	CHECK(firstFrame > 0.0f);
	CHECK(firstFrame < 1.0f);

	Settle(system, -5.0f, -5.0f);
	CHECK(system.GetState().transitionPhase == Approx(1.0f));

	// Surfacing: still running while the crossing unwinds.
	system.Update(10.0f, 5.0f, 0.0f, 10.0f, 5.0f, 0.0f, 1.0f / 60.0f);
	CHECK_FALSE(system.GetState().active);
	CHECK(system.GetState().transitionPhase > 0.0f);
	CHECK(ShouldRunUnderwaterPass(system.GetState()));

	Settle(system, 5.0f, 5.0f);
	CHECK(system.GetState().transitionPhase == Approx(0.0f));
	CHECK_FALSE(ShouldRunUnderwaterPass(system.GetState()));
}

TEST_CASE("WaterVolume_Teleport_Into_Deep_Water_Still_Ramps", "[water_volume]")
{
	// Arriving 40 units down in one frame must not snap the full-strength filter on; it should
	// ramp exactly as a dive does.
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	system.Update(10.0f, -40.0f, 0.0f, 10.0f, -40.0f, 0.0f, 1.0f / 60.0f);

	CHECK(system.GetState().active);
	CHECK(system.GetState().submersionDepth == Approx(40.0f));
	CHECK(system.GetState().transitionPhase < 0.2f);
}

TEST_CASE("WaterVolume_Profile_Values_Reach_The_State", "[water_volume]")
{
	FakeWaterQuery query;
	WaterVolumeSystem system(query);
	system.SetProfileResolver([](uint32) { return MakeOceanProfile(); });

	Settle(system, -3.0f, -3.0f);

	const UnderwaterState& state = system.GetState();
	CHECK(state.fogDensity == Approx(0.035f));
	CHECK(state.fogColor[1] == Approx(0.30f));
	CHECK(state.absorptionColor[0] == Approx(0.45f));
	CHECK(state.causticsStrength == Approx(0.6f));
	CHECK(state.distortionStrength == Approx(0.4f));
}

TEST_CASE("WaterVolume_Missing_Profile_Leaves_Parameters_Zeroed", "[water_volume]")
{
	// An unauthored liquid must render as plain clear water, never inherit whatever the last
	// resolved liquid happened to be.
	FakeWaterQuery query;
	WaterVolumeSystem system(query);
	system.SetProfileResolver([](uint32) { return WaterProfileValues(); });

	Settle(system, -3.0f, -3.0f);

	const UnderwaterState& state = system.GetState();
	CHECK(state.active);
	CHECK(state.fogDensity == Approx(0.0f));
	CHECK(state.causticsStrength == Approx(0.0f));
	CHECK(system.GetAudioLowPassHz() == Approx(0.0f));
}

TEST_CASE("WaterVolume_No_Resolver_Is_Safe", "[water_volume]")
{
	// Before the client installs a resolver the system must still produce usable state rather
	// than calling through a null std::function.
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	Settle(system, -3.0f, -3.0f);

	CHECK(system.GetState().active);
	CHECK(system.GetState().fogDensity == Approx(0.0f));
	CHECK(system.GetAudioLowPassHz() == Approx(0.0f));
}

TEST_CASE("WaterVolume_Water_Type_Change_Reresolves_Profile", "[water_volume]")
{
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	uint32 lastRequested = 0;
	system.SetProfileResolver([&lastRequested](const uint32 type)
	{
		lastRequested = type;
		WaterProfileValues profile = MakeOceanProfile();
		profile.fogDensity = (type == TypeOcean) ? 0.035f : 0.120f;
		return profile;
	});

	Settle(system, -3.0f, -3.0f);
	CHECK(lastRequested == TypeOcean);
	CHECK(system.GetState().fogDensity == Approx(0.035f));

	// Swim from the ocean into a murky lake without surfacing.
	query.waterType = TypeWater;
	Settle(system, -3.0f, -3.0f);

	CHECK(lastRequested == TypeWater);
	CHECK(system.GetState().fogDensity == Approx(0.120f));
}

TEST_CASE("WaterVolume_Audio_Ramps_Independently_Of_The_Camera", "[water_volume]")
{
	// The character dives while the camera stays above the surface. The audio crossing must run
	// on the player's submersion, not the camera's, or the muffling never fades in at all.
	FakeWaterQuery query;
	WaterVolumeSystem system(query);
	system.SetProfileResolver([](uint32) { return MakeOceanProfile(); });

	system.Update(10.0f, 4.0f, 0.0f, 10.0f, -2.0f, 0.0f, 1.0f / 60.0f);

	CHECK_FALSE(system.GetState().active);
	CHECK(system.GetState().transitionPhase == Approx(0.0f));
	// Partway through the audio crossing after one frame: the filter has only begun to close.
	CHECK(system.GetAudioLowPassHz() > 900.0f);
	CHECK(system.GetAudioLowPassHz() < DryLowPassCutoffHz);
}

TEST_CASE("WaterVolume_Audio_Crossing_Sweeps_Monotonically_Without_Undershooting", "[water_volume]")
{
	// Diving, the cutoff may only ever fall from open toward the target, never start closed and
	// open up. Surfacing, it may only rise again, then switch off. Anything else is an audible dip
	// or pop on every crossing.
	FakeWaterQuery query;
	WaterVolumeSystem system(query);
	system.SetProfileResolver([](uint32) { return MakeOceanProfile(); });

	float previous = DryLowPassCutoffHz;
	for (int frame = 0; frame < 60; ++frame)
	{
		system.Update(10.0f, 5.0f, 0.0f, 10.0f, -3.0f, 0.0f, 1.0f / 60.0f);
		const float cutoff = system.GetAudioLowPassHz();
		CHECK(cutoff >= Approx(900.0f));
		CHECK(cutoff <= previous + 0.01f);
		previous = cutoff;
	}
	CHECK(previous == Approx(900.0f));

	for (int frame = 0; frame < 60; ++frame)
	{
		system.Update(10.0f, 5.0f, 0.0f, 10.0f, 5.0f, 0.0f, 1.0f / 60.0f);
		const float cutoff = system.GetAudioLowPassHz();
		if (cutoff == 0.0f)
		{
			break;
		}

		CHECK(cutoff >= Approx(900.0f));
		CHECK(cutoff >= previous - 0.01f);
		previous = cutoff;
	}
	CHECK(system.GetAudioLowPassHz() == Approx(0.0f));
}

TEST_CASE("LowPassCutoffForPhase_Endpoints", "[water_volume]")
{
	CHECK(LowPassCutoffForPhase(900.0f, 0.0f) == Approx(0.0f));
	CHECK(LowPassCutoffForPhase(0.0f, 0.5f) == Approx(0.0f));
	CHECK(LowPassCutoffForPhase(900.0f, 1.0f) == Approx(900.0f));

	// A tiny phase is still essentially open, so switching the filter off from there is silent.
	CHECK(LowPassCutoffForPhase(900.0f, 0.001f) > 21000.0f);

	// A target above the dry cutoff is clamped rather than sweeping upward.
	CHECK(LowPassCutoffForPhase(40000.0f, 0.5f) == Approx(DryLowPassCutoffHz));
}

TEST_CASE("WaterVolume_God_Ray_Toggle_Reaches_The_State", "[water_volume]")
{
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	system.SetGodRaysEnabled(false);
	system.Update(10.0f, -3.0f, 0.0f, 10.0f, -3.0f, 0.0f, 1.0f / 60.0f);

	CHECK_FALSE(system.GetState().godRaysEnabled);
}

TEST_CASE("WaterVolume_Screen_Water_Type_Held_While_Surfacing", "[water_volume]")
{
	FakeWaterQuery query;
	WaterVolumeSystem system(query);

	Settle(system, -3.0f, -3.0f);
	CHECK(system.GetScreenWaterType() == TypeOcean);

	// One frame out of the water: the camera is dry, but the crossing is still fading the fog out,
	// so whatever is presented with it - the caustics texture - must not have switched off yet.
	system.Update(10.0f, 5.0f, 0.0f, 10.0f, 5.0f, 0.0f, 1.0f / 60.0f);
	CHECK(system.GetCameraWaterType() == 0u);
	CHECK(system.GetScreenWaterType() == TypeOcean);

	Settle(system, 5.0f, 5.0f);
	CHECK(system.GetScreenWaterType() == 0u);
}

TEST_CASE("WaterVolume_Surface_Height_Held_When_Leaving_The_Water_Sideways", "[water_volume]")
{
	// Swimming out past the edge of a painted water area whose surface sits at y = 12. For the rest
	// of the crossing the pass must keep measuring against that surface, not against the y = 0 a
	// position without water reports.
	FakeWaterQuery query;
	query.surfaceHeight = 12.0f;
	WaterVolumeSystem system(query);

	Settle(system, 8.0f, 8.0f);
	CHECK(system.GetState().surfaceHeight == Approx(12.0f));

	// x < 0 has no water at all.
	system.Update(-10.0f, 8.0f, 0.0f, -10.0f, 8.0f, 0.0f, 1.0f / 60.0f);
	CHECK_FALSE(system.GetState().active);
	CHECK(system.GetState().transitionPhase > 0.0f);
	CHECK(system.GetState().surfaceHeight == Approx(12.0f));
}

namespace
{
	/// Stands in for proto::WaterProfile and proto_client::WaterProfile: the same accessor names,
	/// so ToWaterProfileValues is exercised without a protobuf dependency. An empty optional
	/// behaves as an unset field, and like protobuf reads back as 0.
	struct FakeProfile
	{
		std::optional<uint32> fogColor;
		std::optional<float> fogDensity;
		std::optional<uint32> absorptionColor;
		std::optional<float> causticsStrength;
		std::optional<float> distortionStrength;
		std::optional<float> audioLowPassHz;

		[[nodiscard]] bool has_fog_color() const { return fogColor.has_value(); }
		[[nodiscard]] uint32 fog_color() const { return fogColor.value_or(0u); }
		[[nodiscard]] bool has_fog_density() const { return fogDensity.has_value(); }
		[[nodiscard]] float fog_density() const { return fogDensity.value_or(0.0f); }
		[[nodiscard]] bool has_absorption_color() const { return absorptionColor.has_value(); }
		[[nodiscard]] uint32 absorption_color() const { return absorptionColor.value_or(0u); }
		[[nodiscard]] bool has_caustics_strength() const { return causticsStrength.has_value(); }
		[[nodiscard]] float caustics_strength() const { return causticsStrength.value_or(0.0f); }
		[[nodiscard]] bool has_distortion_strength() const { return distortionStrength.has_value(); }
		[[nodiscard]] float distortion_strength() const { return distortionStrength.value_or(0.0f); }
		[[nodiscard]] bool has_audio_lowpass_hz() const { return audioLowPassHz.has_value(); }
		[[nodiscard]] float audio_lowpass_hz() const { return audioLowPassHz.value_or(0.0f); }
	};
}

TEST_CASE("WaterProfile_Conversion_Unpacks_Colors_And_Zeroes_Unset_Fields", "[water_volume]")
{
	FakeProfile profile;
	profile.fogColor = 0xFF0A4257u;
	profile.fogDensity = 0.03f;
	profile.causticsStrength = 0.5f;

	const WaterProfileValues values = ToWaterProfileValues(profile);

	CHECK(values.valid);

	// Packed 0xAARRGGBB: red is the third byte from the right, blue the lowest.
	CHECK(values.fogColor[0] == Approx(static_cast<float>(0x0A) / 255.0f));
	CHECK(values.fogColor[1] == Approx(static_cast<float>(0x42) / 255.0f));
	CHECK(values.fogColor[2] == Approx(static_cast<float>(0x57) / 255.0f));
	CHECK(values.fogDensity == Approx(0.03f));
	CHECK(values.causticsStrength == Approx(0.5f));

	// Unset fields disable their effect.
	CHECK(values.absorptionColor[0] == Approx(0.0f));
	CHECK(values.absorptionColor[2] == Approx(0.0f));
	CHECK(values.distortionStrength == Approx(0.0f));
	CHECK(values.audioLowPassHz == Approx(0.0f));
}
