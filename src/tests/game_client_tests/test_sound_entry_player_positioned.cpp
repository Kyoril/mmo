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

namespace
{
	proto_client::SoundEntry* AddLooped3DEntry(proto_client::SoundManager& sounds, const uint32 id)
	{
		auto* entry = sounds.add(id);
		entry->add_files("Sound/Glider/GliderWindLoop.wav");
		entry->set_looped(true);
		entry->set_is_3d(true);
		return entry;
	}
}

TEST_CASE("MoveChannel follows a moving unit with a playing voice", "[sound_entry_player]")
{
	FakeAudio audio;
	proto_client::SoundManager sounds;
	REQUIRE(AddLooped3DEntry(sounds, 452) != nullptr);

	SoundEntryPlayer player(audio, sounds);
	const ChannelIndex channel = player.PlayEntry(452, Vector3(1.0f, 2.0f, 3.0f));
	REQUIRE(channel != InvalidChannel);

	player.MoveChannel(channel, Vector3(10.0f, 20.0f, 30.0f));
	CHECK(audio.channelPositions[channel].x == Approx(10.0f));
	CHECK(audio.channelPositions[channel].z == Approx(30.0f));
}

TEST_CASE("StopChannel stops the voice and forgets the channel", "[sound_entry_player]")
{
	FakeAudio audio;
	proto_client::SoundManager sounds;
	REQUIRE(AddLooped3DEntry(sounds, 452) != nullptr);

	SoundEntryPlayer player(audio, sounds);
	ChannelIndex channel = player.PlayEntry(452, Vector3::Zero);
	REQUIRE(channel != InvalidChannel);
	const ChannelIndex started = channel;

	player.StopChannel(channel);
	CHECK(audio.stoppedChannels.count(started) == 1);
	CHECK(channel == InvalidChannel);

	// Stopping again is harmless.
	player.StopChannel(channel);
	CHECK(channel == InvalidChannel);
}

TEST_CASE("a positioned entry can be played at a raised priority", "[sound_entry_player]")
{
	FakeAudio audio;
	proto_client::SoundManager sounds;
	REQUIRE(AddLooped3DEntry(sounds, 452) != nullptr);

	SoundEntryPlayer player(audio, sounds);
	const ChannelIndex channel = player.PlayEntry(452, Vector3::Zero, 1.5f);
	REQUIRE(channel != InvalidChannel);
	CHECK(audio.channelPriorities[channel] == Approx(1.5f));
}
