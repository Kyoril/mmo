// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_audio.h"

#include "game_client/sound_entry_player.h"

using namespace mmo;

TEST_CASE("PlayEntryPositionedLoop starts a positioned voice", "[sound_entry_player]")
{
	FakeAudio audio;
	proto_client::SoundManager sounds;
	auto* entry = sounds.add(7);
	REQUIRE(entry != nullptr);
	entry->add_files("Sound/Ambience/Water/RiverCalm.wav");
	entry->set_looped(true);
	entry->set_is_3d(false);
	entry->set_stream(true);
	entry->set_category(static_cast<proto_client::SoundEntryCategory>(2));
	entry->set_volume(0.5f);

	SoundEntryPlayer player(audio, sounds);
	const ChannelIndex channel = player.PlayEntryPositionedLoop(7, Vector3(3.0f, 0.0f, 4.0f), 5.0f, 60.0f);

	REQUIRE(channel != InvalidChannel);
	CHECK(audio.PlayedFile("Sound/Ambience/Water/RiverCalm.wav"));
	CHECK(audio.channelPositions[channel].x == Approx(3.0f));
	CHECK(audio.GetChannelVolume(channel) == Approx(0.5f));
}

TEST_CASE("PlayEntryPositionedLoop rejects unknown entries", "[sound_entry_player]")
{
	FakeAudio audio;
	proto_client::SoundManager sounds;
	SoundEntryPlayer player(audio, sounds);
	CHECK(player.PlayEntryPositionedLoop(99, Vector3::Zero, 1.0f, 2.0f) == InvalidChannel);
}

TEST_CASE("Set3DSpread is recorded on the channel", "[sound_entry_player]")
{
	FakeAudio audio;
	audio.GetChannelInstance(0)->Set3DSpread(120.0f);
	CHECK(audio.GetChannelSpread(0) == Approx(120.0f));
}
