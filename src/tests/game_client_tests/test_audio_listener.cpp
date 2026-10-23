// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_audio.h"

#include "game_client/audio_listener.h"

using namespace mmo;

TEST_CASE("FlattenedForward drops pitch and normalises", "[audio_listener]")
{
	const Vector3 f = audio_listener::FlattenedForward(Vector3(0.0f, -0.8f, -0.6f));
	CHECK(f.x == Approx(0.0f));
	CHECK(f.y == Approx(0.0f));
	CHECK(f.z == Approx(-1.0f));
}

TEST_CASE("FlattenedForward keeps the heading of a diagonal view", "[audio_listener]")
{
	const Vector3 f = audio_listener::FlattenedForward(Vector3(3.0f, 0.5f, -4.0f));
	CHECK(f.x == Approx(0.6f));
	CHECK(f.y == Approx(0.0f));
	CHECK(f.z == Approx(-0.8f));
}

TEST_CASE("FlattenedForward looking straight down falls back to -Z", "[audio_listener]")
{
	const Vector3 f = audio_listener::FlattenedForward(Vector3(0.0f, -1.0f, 0.0f));
	CHECK(f.z == Approx(-1.0f));
}

TEST_CASE("FakeAudio records listener orientation", "[audio_listener]")
{
	FakeAudio audio;
	audio.SetListenerOrientation(Vector3(1, 0, 0), Vector3(0, 1, 0));
	CHECK(audio.listenerForward.x == Approx(1.0f));
}
