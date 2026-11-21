// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The timeline of a cutscene (shared/game/cutscene.h) is evaluated on both sides: the client flies
// its camera along it, and the world server follows the same path with the player's point of view.
// Both must agree on how long a cutscene is and where its camera is at any moment.

#include "catch.hpp"

#include "game/cutscene.h"
#include "shared/proto_data/cutscenes.pb.h"

using namespace mmo;

namespace
{
	proto::CutsceneKeyframe* AddKeyframe(proto::CutsceneShot& shot, const uint32 time, const Vector3& position, const float yaw = 0.0f, const float pitch = 0.0f)
	{
		auto* keyframe = shot.add_keyframes();
		keyframe->set_time(time);
		keyframe->set_x(position.x);
		keyframe->set_y(position.y);
		keyframe->set_z(position.z);
		keyframe->set_yaw(yaw);
		keyframe->set_pitch(pitch);
		return keyframe;
	}

	proto::CutsceneEntry MakeCutscene()
	{
		proto::CutsceneEntry cutscene;
		cutscene.set_id(1);
		cutscene.set_name("Test");
		cutscene.set_map(0);
		return cutscene;
	}
}

TEST_CASE("A cutscene is as long as its shots one after another", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();
	CHECK(GetCutsceneDuration(cutscene) == 0);
	CHECK_FALSE(IsCutscenePlayable(cutscene));

	auto* first = cutscene.add_shots();
	AddKeyframe(*first, 0, Vector3(0.0f, 0.0f, 0.0f));
	AddKeyframe(*first, 4000, Vector3(40.0f, 0.0f, 0.0f));

	// A shot without keyframes has no length and does not count
	cutscene.add_shots();

	auto* third = cutscene.add_shots();
	AddKeyframe(*third, 0, Vector3(500.0f, 10.0f, 0.0f));
	AddKeyframe(*third, 2500, Vector3(500.0f, 10.0f, 50.0f));

	CHECK(GetCutsceneShotDuration(*first) == 4000);
	CHECK(GetCutsceneDuration(cutscene) == 6500);
	CHECK(IsCutscenePlayable(cutscene));
}

TEST_CASE("A cutscene without anything to play cannot be sampled", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();
	cutscene.add_shots();

	CutsceneSample sample;
	sample.position = Vector3(1.0f, 2.0f, 3.0f);
	CHECK_FALSE(SampleCutscene(cutscene, 0.0f, sample));
	CHECK(sample.position == Vector3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("The camera passes through every keyframe at its time", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();
	auto* shot = cutscene.add_shots();
	AddKeyframe(*shot, 0, Vector3(0.0f, 10.0f, 0.0f), 0.0f, -10.0f);
	AddKeyframe(*shot, 2000, Vector3(20.0f, 30.0f, 5.0f), 45.0f, -20.0f);
	AddKeyframe(*shot, 7000, Vector3(100.0f, 15.0f, -40.0f), 90.0f, -5.0f);

	for (const uint32 interpolation : { static_cast<uint32>(cutscene_interpolation::Smooth), static_cast<uint32>(cutscene_interpolation::Linear) })
	{
		shot->set_interpolation(interpolation);

		for (int i = 0; i < shot->keyframes_size(); ++i)
		{
			const auto& keyframe = shot->keyframes(i);

			CutsceneSample sample;
			REQUIRE(SampleCutscene(cutscene, static_cast<float>(keyframe.time()), sample));
			CHECK(sample.position.x == Approx(keyframe.x()).margin(0.01));
			CHECK(sample.position.y == Approx(keyframe.y()).margin(0.01));
			CHECK(sample.position.z == Approx(keyframe.z()).margin(0.01));
			CHECK(sample.yaw == Approx(keyframe.yaw()).margin(0.01));
			CHECK(sample.pitch == Approx(keyframe.pitch()).margin(0.01));
		}
	}
}

TEST_CASE("Straight interpolation is halfway between two keyframes at half time", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();
	auto* shot = cutscene.add_shots();
	shot->set_interpolation(cutscene_interpolation::Linear);
	AddKeyframe(*shot, 0, Vector3(0.0f, 0.0f, 0.0f));
	AddKeyframe(*shot, 1000, Vector3(100.0f, 20.0f, -60.0f));

	CutsceneSample sample;
	REQUIRE(SampleCutscene(cutscene, 500.0f, sample));
	CHECK(sample.position.x == Approx(50.0f));
	CHECK(sample.position.y == Approx(10.0f));
	CHECK(sample.position.z == Approx(-30.0f));
}

TEST_CASE("A smooth path stays between its keyframes on a straight line", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();
	auto* shot = cutscene.add_shots();
	AddKeyframe(*shot, 0, Vector3(0.0f, 0.0f, 0.0f));
	AddKeyframe(*shot, 1000, Vector3(10.0f, 0.0f, 0.0f));
	AddKeyframe(*shot, 2000, Vector3(20.0f, 0.0f, 0.0f));
	AddKeyframe(*shot, 3000, Vector3(30.0f, 0.0f, 0.0f));

	// Evenly spaced keys on a line: the curve is that line, travelled at a steady speed
	for (float time = 0.0f; time <= 3000.0f; time += 250.0f)
	{
		CutsceneSample sample;
		REQUIRE(SampleCutscene(cutscene, time, sample));
		CHECK(sample.position.x == Approx(time / 100.0f).margin(0.01));
		CHECK(sample.position.y == Approx(0.0f).margin(0.001));
	}
}

TEST_CASE("The camera turns the short way around", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();
	auto* shot = cutscene.add_shots();
	shot->set_interpolation(cutscene_interpolation::Linear);
	AddKeyframe(*shot, 0, Vector3::Zero, 350.0f);
	AddKeyframe(*shot, 1000, Vector3::Zero, 10.0f);

	// 350 -> 10 degrees is a turn of 20 degrees through north, not of 340 degrees the other way
	CutsceneSample sample;
	REQUIRE(SampleCutscene(cutscene, 500.0f, sample));
	CHECK(sample.yaw == Approx(360.0f));
}

TEST_CASE("Shots follow one another and the next shot starts at its own first keyframe", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();

	auto* first = cutscene.add_shots();
	first->set_interpolation(cutscene_interpolation::Linear);
	AddKeyframe(*first, 0, Vector3(0.0f, 0.0f, 0.0f));
	AddKeyframe(*first, 1000, Vector3(10.0f, 0.0f, 0.0f));

	auto* second = cutscene.add_shots();
	second->set_interpolation(cutscene_interpolation::Linear);
	AddKeyframe(*second, 0, Vector3(900.0f, 50.0f, 900.0f));
	AddKeyframe(*second, 2000, Vector3(920.0f, 50.0f, 900.0f));

	CutsceneSample sample;

	REQUIRE(SampleCutscene(cutscene, 999.0f, sample));
	CHECK(sample.shot == 0);
	CHECK(sample.position.x == Approx(9.99f).margin(0.01));

	// The cut: no travelling between the two cameras
	REQUIRE(SampleCutscene(cutscene, 1000.0f, sample));
	CHECK(sample.shot == 1);
	CHECK(sample.position.x == Approx(900.0f));

	REQUIRE(SampleCutscene(cutscene, 2000.0f, sample));
	CHECK(sample.shot == 1);
	CHECK(sample.position.x == Approx(910.0f));

	// Past the end the camera rests on the last keyframe
	REQUIRE(SampleCutscene(cutscene, 60000.0f, sample));
	CHECK(sample.shot == 1);
	CHECK(sample.position.x == Approx(920.0f));
}

TEST_CASE("A shot fades in from black and out to black", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();
	auto* shot = cutscene.add_shots();
	shot->set_fade_in(1000);
	shot->set_fade_out(2000);
	AddKeyframe(*shot, 0, Vector3::Zero);
	AddKeyframe(*shot, 10000, Vector3(10.0f, 0.0f, 0.0f));

	CutsceneSample sample;

	REQUIRE(SampleCutscene(cutscene, 0.0f, sample));
	CHECK(sample.fade == Approx(1.0f));

	REQUIRE(SampleCutscene(cutscene, 500.0f, sample));
	CHECK(sample.fade == Approx(0.5f));

	REQUIRE(SampleCutscene(cutscene, 5000.0f, sample));
	CHECK(sample.fade == Approx(0.0f));

	REQUIRE(SampleCutscene(cutscene, 9000.0f, sample));
	CHECK(sample.fade == Approx(0.5f));

	REQUIRE(SampleCutscene(cutscene, 10000.0f, sample));
	CHECK(sample.fade == Approx(1.0f));
}

TEST_CASE("A field of view of zero keeps the player camera's", "[cutscene]")
{
	proto::CutsceneEntry cutscene = MakeCutscene();
	auto* shot = cutscene.add_shots();
	AddKeyframe(*shot, 0, Vector3::Zero)->set_fov(40.0f);
	AddKeyframe(*shot, 1000, Vector3(1.0f, 0.0f, 0.0f));

	// One keyframe leaves it open: the shot does not zoom towards a field of view of nothing
	CutsceneSample sample;
	REQUIRE(SampleCutscene(cutscene, 500.0f, sample));
	CHECK(sample.fov == 0.0f);

	shot->mutable_keyframes(1)->set_fov(60.0f);
	shot->set_interpolation(cutscene_interpolation::Linear);
	REQUIRE(SampleCutscene(cutscene, 500.0f, sample));
	CHECK(sample.fov == Approx(50.0f));
}
