// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// When the glider plays its open and close sounds and runs its wind loop.

#include "catch.hpp"

#include "game_client/glider_sound_cues.h"

using namespace mmo;

TEST_CASE("opening the glider plays the open sound and starts the wind loop", "[glider][sound]")
{
	GliderSoundCues cues;
	const auto cue = cues.Update(true, glider_event::Opened);
	CHECK(cue.playOpen);
	CHECK(cue.startLoop);
	CHECK_FALSE(cue.playClose);
	CHECK_FALSE(cue.stopLoop);
	CHECK(cues.IsLoopRunning());
}

TEST_CASE("gliding on plays nothing new", "[glider][sound]")
{
	GliderSoundCues cues;
	cues.Update(true, glider_event::Opened);
	const auto cue = cues.Update(true, glider_event::None);
	CHECK_FALSE(cue.playOpen);
	CHECK_FALSE(cue.startLoop);
	CHECK_FALSE(cue.playClose);
	CHECK_FALSE(cue.stopLoop);
}

TEST_CASE("closing plays the close sound and stops the loop once, when folding starts", "[glider][sound]")
{
	GliderSoundCues cues;
	cues.Update(true, glider_event::Opened);

	const auto cue = cues.Update(false, glider_event::None);
	CHECK(cue.playClose);
	CHECK(cue.stopLoop);
	CHECK_FALSE(cues.IsLoopRunning());

	const auto folded = cues.Update(false, glider_event::Closed);
	CHECK_FALSE(folded.playClose);
	CHECK_FALSE(folded.stopLoop);
}

TEST_CASE("re-opening while still folding restarts the loop without a second open sound", "[glider][sound]")
{
	GliderSoundCues cues;
	cues.Update(true, glider_event::Opened);
	cues.Update(false, glider_event::None);

	const auto cue = cues.Update(true, glider_event::None);
	CHECK_FALSE(cue.playOpen);
	CHECK(cue.startLoop);
	CHECK(cues.IsLoopRunning());
}

TEST_CASE("a glider rebuilt mid-glide (display change) does not play the open sound again", "[glider][sound]")
{
	GliderSoundCues cues;
	cues.Update(true, glider_event::Opened);

	// The new GliderVisual reports Opened again on its first frame.
	const auto cue = cues.Update(true, glider_event::Opened);
	CHECK_FALSE(cue.playOpen);
	CHECK_FALSE(cue.startLoop);
}
