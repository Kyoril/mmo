// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/sound_entry_player.h"

#include <map>
#include <set>
#include <vector>

using namespace mmo;

namespace
{
	/// Fake channel instance which simply records the volume set on it.
	class FakeChannelInstance final : public IChannelInstance
	{
	public:
		void Clear() override
		{
		}

		void SetPitch(const float value) override
		{
			m_pitch = value;
		}

		float GetPitch() const override
		{
			return m_pitch;
		}

		void SetVolume(const float volume) override
		{
			m_volume = volume;
		}

		float GetVolume() const override
		{
			return m_volume;
		}

	private:
		float m_pitch = 1.0f;
		float m_volume = 1.0f;
	};

	/// Headless IAudio double: hands out incrementing sound / channel indices and
	/// records which channels were started and stopped.
	class FakeAudio final : public IAudio
	{
	public:
		std::vector<ChannelIndex> startedChannels;
		std::set<ChannelIndex> stoppedChannels;

		void Create() override
		{
		}

		void Destroy() override
		{
		}

		void Update(const Vector3&, float) override
		{
		}

		SoundIndex CreateSound(const String&) override
		{
			return m_nextSound++;
		}

		SoundIndex CreateStream(const String&) override
		{
			return m_nextSound++;
		}

		SoundIndex CreateLoopedSound(const String&) override
		{
			return m_nextSound++;
		}

		SoundIndex CreateLoopedStream(const String&) override
		{
			return m_nextSound++;
		}

		SoundIndex CreateSound(const String&, SoundType) override
		{
			return m_nextSound++;
		}

		void PlaySound(SoundIndex, ChannelIndex* channelIndex, float, SoundCategory) override
		{
			*channelIndex = m_nextChannel++;
			startedChannels.push_back(*channelIndex);
		}

		void PlaySound3D(SoundIndex, ChannelIndex* channelIndex, const Vector3&, float, float, float, SoundCategory) override
		{
			*channelIndex = m_nextChannel++;
			startedChannels.push_back(*channelIndex);
		}

		void StopSound(ChannelIndex* channelIndex) override
		{
			if (channelIndex && *channelIndex != InvalidChannel)
			{
				stoppedChannels.insert(*channelIndex);
				*channelIndex = InvalidChannel;
			}
		}

		void StopAllSounds() override
		{
		}

		SoundIndex FindSound(const String&, SoundType) override
		{
			return InvalidSound;
		}

		void Set3DMinMaxDistance(ChannelIndex, float, float) override
		{
		}

		void Set3DPosition(ChannelIndex, const Vector3&) override
		{
		}

		float GetSoundLength(SoundIndex) override
		{
			return 0.0f;
		}

		ISoundInstance* GetSoundInstance(SoundIndex) override
		{
			return nullptr;
		}

		IChannelInstance* GetChannelInstance(const ChannelIndex channel) override
		{
			return &m_channels[channel];
		}

		void SetMasterVolume(float) override
		{
		}

		void SetMasterMuted(bool) override
		{
		}

		void SetCategoryVolume(SoundCategory, float) override
		{
		}

		void SetCategoryMuted(SoundCategory, bool) override
		{
		}

		float GetChannelVolume(const ChannelIndex channel)
		{
			return m_channels[channel].GetVolume();
		}

	private:
		SoundIndex m_nextSound = 0;
		ChannelIndex m_nextChannel = 0;
		std::map<ChannelIndex, FakeChannelInstance> m_channels;
	};

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
