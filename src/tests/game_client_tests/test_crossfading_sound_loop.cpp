// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_audio.h"

#include "game_client/sound_entry_player.h"

using namespace mmo;

namespace
{
	void addLoopedEntry(proto_client::SoundManager& sounds, const uint32 id, const uint32 fadeMs)
	{
		auto* entry = sounds.add(id);
		REQUIRE(entry != nullptr);
		entry->add_files("Sound/test.wav");
		entry->set_looped(true);
		entry->set_volume(1.0f);
		entry->set_fade_in_ms(fadeMs);
		entry->set_fade_out_ms(fadeMs);
	}
}

TEST_CASE("CrossfadingSoundLoop crossfades between two sounds", "[crossfading_sound_loop]")
{
	FakeAudio audio;
	proto_client::SoundManager sounds;
	addLoopedEntry(sounds, 1, 1000);
	addLoopedEntry(sounds, 2, 1000);

	SoundEntryPlayer player(audio, sounds);
	CrossfadingSoundLoop loop(player, audio);

	loop.SetSound(1);
	REQUIRE(audio.startedChannels.size() == 1);
	const ChannelIndex channelA = audio.startedChannels[0];

	loop.Update(2.0f);
	CHECK(audio.GetChannelVolume(channelA) == Approx(1.0f));

	loop.SetSound(2);
	REQUIRE(audio.startedChannels.size() == 2);
	const ChannelIndex channelB = audio.startedChannels[1];
	CHECK(loop.GetCurrentSoundId() == 2);

	// Halfway through the crossfade the old sound is at half volume fading out
	// while the new one is at half volume fading in.
	loop.Update(0.5f);
	CHECK(audio.GetChannelVolume(channelA) == Approx(0.5f));
	CHECK(audio.GetChannelVolume(channelB) == Approx(0.5f));

	// After the full fade-out duration the old sound has been stopped.
	loop.Update(1.0f);
	CHECK(audio.stoppedChannels.count(channelA) == 1);
	CHECK(audio.GetChannelVolume(channelB) == Approx(1.0f));
}

TEST_CASE("CrossfadingSoundLoop reclaims a fading-out sound instead of restarting it", "[crossfading_sound_loop]")
{
	FakeAudio audio;
	proto_client::SoundManager sounds;
	addLoopedEntry(sounds, 1, 1000);
	addLoopedEntry(sounds, 2, 1000);

	SoundEntryPlayer player(audio, sounds);
	CrossfadingSoundLoop loop(player, audio);

	// Zone A music plays at full volume.
	loop.SetSound(1);
	loop.Update(2.0f);
	REQUIRE(audio.startedChannels.size() == 1);
	const ChannelIndex channelA = audio.startedChannels[0];

	// Cross into zone B: A fades out while B fades in.
	loop.SetSound(2);
	REQUIRE(audio.startedChannels.size() == 2);
	const ChannelIndex channelB = audio.startedChannels[1];
	loop.Update(0.5f);

	// Quickly cross back into zone A while its music is still fading out. The
	// audible copy must be kept and faded back in, not cut off and restarted.
	loop.SetSound(1);
	CHECK(loop.GetCurrentSoundId() == 1);
	CHECK(audio.stoppedChannels.count(channelA) == 0);
	CHECK(audio.startedChannels.size() == 2);
	CHECK(audio.GetChannelVolume(channelA) == Approx(0.5f));

	// The reclaimed sound fades back up from where it was...
	loop.Update(0.25f);
	CHECK(audio.GetChannelVolume(channelA) == Approx(0.75f));

	// ...while the outgoing sound finishes its fade-out and is stopped.
	loop.Update(1.0f);
	CHECK(audio.GetChannelVolume(channelA) == Approx(1.0f));
	CHECK(audio.stoppedChannels.count(channelB) == 1);
}

TEST_CASE("CrossfadingSoundLoop reclaim with instant fade-in snaps to full volume", "[crossfading_sound_loop]")
{
	FakeAudio audio;
	proto_client::SoundManager sounds;

	// Sound 1 fades out over a second but has no fade-in configured.
	auto* entry = sounds.add(1);
	REQUIRE(entry != nullptr);
	entry->add_files("Sound/test.wav");
	entry->set_looped(true);
	entry->set_volume(0.8f);
	entry->set_fade_in_ms(0);
	entry->set_fade_out_ms(1000);
	addLoopedEntry(sounds, 2, 1000);

	SoundEntryPlayer player(audio, sounds);
	CrossfadingSoundLoop loop(player, audio);

	loop.SetSound(1);
	REQUIRE(audio.startedChannels.size() == 1);
	const ChannelIndex channelA = audio.startedChannels[0];
	loop.Update(2.0f);

	loop.SetSound(2);
	loop.Update(0.5f);

	loop.SetSound(1);
	CHECK(loop.GetCurrentSoundId() == 1);
	CHECK(audio.stoppedChannels.count(channelA) == 0);

	// Instant fade-in: the reclaimed channel returns to its base volume right away.
	CHECK(audio.GetChannelVolume(channelA) == Approx(0.8f));
}
