// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_audio.h"
#include "fake_water_source.h"

#include "game_client/sound_entry_player.h"
#include "game_client/water_ambience.h"

#include <algorithm>

using namespace mmo;

namespace
{
	constexpr uint32 CalmEntry = 1;
	constexpr uint32 FastEntry = 2;
	constexpr uint32 SwimEntry = 3;
	constexpr uint32 ShoreEntry = 4;

	constexpr float EntryVolume = 0.8f;
	constexpr float FrameSeconds = 0.05f;
	constexpr uint32 Seed = 1234;

	const char* const CalmFile = "Sound/Ambience/Water/RiverCalm.wav";
	const char* const FastFile = "Sound/Ambience/Water/RiverFast.wav";
	const char* const SwimFile = "Sound/Ambience/Water/Swim.wav";
	const char* const ShoreFile = "Sound/Ambience/Water/Shore.wav";

	void AddEntry(proto_client::SoundManager& sounds, const uint32 id, const char* file)
	{
		auto* entry = sounds.add(id);
		REQUIRE(entry != nullptr);
		entry->add_files(file);
		entry->set_looped(true);
		entry->set_is_3d(false);
		entry->set_stream(true);
		entry->set_category(static_cast<proto_client::SoundEntryCategory>(2));
		entry->set_volume(EntryVolume);
	}

	/// Sound entries 1 calm, 2 fast, 3 swim, 4 shore.
	struct Fixture
	{
		FakeAudio audio;
		proto_client::SoundManager sounds;
		std::unique_ptr<SoundEntryPlayer> player;
		std::unique_ptr<WaterAmbience> ambience;

		Fixture()
		{
			AddEntry(sounds, CalmEntry, CalmFile);
			AddEntry(sounds, FastEntry, FastFile);
			AddEntry(sounds, SwimEntry, SwimFile);
			AddEntry(sounds, ShoreEntry, ShoreFile);
			player = std::make_unique<SoundEntryPlayer>(audio, sounds);
			ambience = std::make_unique<WaterAmbience>(*player, audio, Seed);
		}

		void Run(const IWaterAmbienceSource& source, const float seconds, const bool swimming = false, const uint32 swimType = 0)
		{
			const int frames = static_cast<int>(seconds / FrameSeconds + 0.5f);
			for (int i = 0; i < frames; ++i)
			{
				ambience->Update(source, Vector3::Zero, swimming, swimType, FrameSeconds);
			}
		}

		/// Channel the given file was started on, or InvalidChannel.
		ChannelIndex ChannelOf(const char* file) const
		{
			const auto it = std::find(audio.playedFiles.begin(), audio.playedFiles.end(), String(file));
			if (it == audio.playedFiles.end())
			{
				return InvalidChannel;
			}

			return audio.startedChannels[static_cast<size_t>(it - audio.playedFiles.begin())];
		}
	};

	WaterAmbienceSounds RiverProfile(const float volume = 1.0f)
	{
		WaterAmbienceSounds sounds;
		sounds.profile.waterType = 1;
		sounds.profile.range = 40.0f;
		sounds.calmSound = CalmEntry;
		sounds.fastSound = FastEntry;
		sounds.swimSound = SwimEntry;
		sounds.volume = volume;
		return sounds;
	}

	WaterSample Dry()
	{
		return WaterSample();
	}
}

TEST_CASE("river on the left starts one calm voice to the left", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile() });

	const FakeWaterSource source([](const float x, float)
	{
		return x < -10.0f ? MakeWater(1, 0.0f) : Dry();
	});

	f.Run(source, 1.0f);

	REQUIRE(f.audio.startedChannels.size() == 1);
	CHECK(f.audio.playedFiles[0] == CalmFile);
	CHECK(f.audio.channelPositions[f.audio.startedChannels[0]].x < 0.0f);

	const auto voices = f.ambience->GetVoices();
	REQUIRE(voices.size() == 1);
	CHECK(voices[0].soundEntryId == CalmEntry);

	// The direction keeps its magnitude (the focus): no normalisation.
	const Vector3& direction = f.ambience->GetSmoothedFields()[0].direction;
	CHECK(voices[0].position.x == Approx(direction.x * water_ambience::VirtualDistance).margin(0.01f));
	CHECK(voices[0].position.x < -0.5f * water_ambience::VirtualDistance);
}

TEST_CASE("fast flow crossfades to the fast loop", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile() });

	const FakeWaterSource source([](float, float)
	{
		return MakeWater(1, 0.0f, 1.0f);
	});

	f.Run(source, 3.0f);

	const ChannelIndex fast = f.ChannelOf(FastFile);
	REQUIRE(fast != InvalidChannel);
	const ChannelIndex calm = f.ChannelOf(CalmFile);

	const float fastVolume = f.audio.GetChannelVolume(fast);
	const float calmVolume = calm == InvalidChannel ? 0.0f : f.audio.GetChannelVolume(calm);
	CHECK(fastVolume > calmVolume);

	const float level = f.ambience->GetSmoothedFields()[0].level;
	CHECK(level > 0.5f);
	const float expected = level * level * EntryVolume * EntryVolume;
	CHECK(calmVolume * calmVolume + fastVolume * fastVolume == Approx(expected).epsilon(0.05));
}

TEST_CASE("smoothing has no step", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile() });

	bool water = false;
	const FakeWaterSource source([&water](float, float)
	{
		return water ? MakeWater(1, 0.0f) : Dry();
	});

	f.Run(source, 1.0f);
	CHECK(f.ambience->GetSmoothedFields()[0].level == 0.0f);

	water = true;

	// The next scan happens within one scan interval; the first frame that sees the water
	// must only move part of the way.
	float level = 0.0f;
	for (int i = 0; i < 10 && level <= 0.0f; ++i)
	{
		f.ambience->Update(source, Vector3::Zero, false, 0, FrameSeconds);
		level = f.ambience->GetSmoothedFields()[0].level;
	}

	CHECK(level > 0.0f);
	CHECK(level < 0.2f);
}

TEST_CASE("voice survives a short silence", "[water_ambience]")
{
	// A quiet profile drops below the audible threshold quickly, so the silence below is
	// mostly time spent under the threshold rather than time spent fading out.
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile(0.01f) });

	bool water = true;
	const FakeWaterSource source([&water](const float x, float)
	{
		return water && x < -10.0f ? MakeWater(1, 0.0f) : Dry();
	});

	f.Run(source, 2.0f);
	REQUIRE(f.audio.startedChannels.size() == 1);

	water = false;
	f.Run(source, 2.0f);
	CHECK(f.audio.GetChannelVolume(f.audio.startedChannels[0]) <= 0.001f);

	water = true;
	f.Run(source, 1.0f);

	CHECK(f.audio.startedChannels.size() == 1);
	CHECK(f.audio.stoppedChannels.empty());
}

TEST_CASE("voice stops after the hold time", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile(0.01f) });

	bool water = true;
	const FakeWaterSource source([&water](const float x, float)
	{
		return water && x < -10.0f ? MakeWater(1, 0.0f) : Dry();
	});

	f.Run(source, 2.0f);
	REQUIRE(f.audio.startedChannels.size() == 1);
	const ChannelIndex channel = f.audio.startedChannels[0];

	water = false;
	f.Run(source, 1.5f);
	CHECK(f.audio.stoppedChannels.count(channel) == 0);

	f.Run(source, 3.0f);
	CHECK(f.audio.stoppedChannels.count(channel) == 1);
	CHECK(f.ambience->GetVoices().empty());
}

TEST_CASE("surrounded widens the spread", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile() });

	const FakeWaterSource source([](float, float)
	{
		return MakeWater(1, 0.0f);
	});

	f.Run(source, 1.0f);

	const ChannelIndex calm = f.ChannelOf(CalmFile);
	REQUIRE(calm != InvalidChannel);
	CHECK(f.audio.GetChannelSpread(calm) > 250.0f);
}

TEST_CASE("one-sided water keeps the spread narrow", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile() });

	const FakeWaterSource source([](const float x, float)
	{
		return x < -10.0f ? MakeWater(1, 0.0f) : Dry();
	});

	f.Run(source, 1.0f);

	const ChannelIndex calm = f.ChannelOf(CalmFile);
	REQUIRE(calm != InvalidChannel);
	CHECK(f.audio.GetChannelSpread(calm) < 250.0f);
	CHECK(f.audio.GetChannelSpread(calm) >= water_ambience::MinSpread);
}

TEST_CASE("swim layer fades in while swimming", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile() });

	const FakeWaterSource source([](float, float)
	{
		return Dry();
	});

	f.Run(source, 0.25f, true, 1);

	const ChannelIndex swim = f.ChannelOf(SwimFile);
	REQUIRE(swim != InvalidChannel);
	const float volume = f.audio.GetChannelVolume(swim);
	CHECK(volume > 0.3f * EntryVolume);
	CHECK(volume < 0.8f * EntryVolume);

	// Leaving the water fades it out and stops it.
	f.Run(source, 1.0f, false, 0);
	CHECK(f.audio.stoppedChannels.count(swim) == 1);
}

TEST_CASE("missing swim sound is silent", "[water_ambience]")
{
	Fixture f;
	WaterAmbienceSounds profile = RiverProfile();
	profile.swimSound = 0;
	f.ambience->SetProfiles({ profile });

	const FakeWaterSource source([](float, float)
	{
		return Dry();
	});

	f.Run(source, 1.0f, true, 1);

	CHECK(f.audio.startedChannels.empty());
}

TEST_CASE("shared sound entries never run two voices", "[water_ambience]")
{
	Fixture f;
	WaterAmbienceSounds ocean = RiverProfile();
	ocean.profile.waterType = 2;
	ocean.fastSound = 0;
	f.ambience->SetProfiles({ RiverProfile(), ocean });

	const FakeWaterSource source([](const float x, float)
	{
		return x < 0.0f ? MakeWater(1, 0.0f) : MakeWater(2, 0.0f);
	});

	f.Run(source, 2.0f);

	CHECK(std::count(f.audio.playedFiles.begin(), f.audio.playedFiles.end(), String(CalmFile)) == 1);
	CHECK(f.ambience->GetVoices().size() == 1);
}

TEST_CASE("a shared bed entry glides between its profiles", "[water_ambience]")
{
	Fixture f;
	WaterAmbienceSounds left = RiverProfile();
	left.fastSound = 0;
	WaterAmbienceSounds right = left;
	right.profile.waterType = 2;
	f.ambience->SetProfiles({ left, right });

	// Loud water to the left and quiet (deep) water to the right; over time the left water
	// sinks and the right water rises, swapping which profile is louder.
	constexpr float DeepSurface = -35.0f;
	float blend = 0.0f;
	const FakeWaterSource source([&blend](const float x, float)
	{
		if (x < -10.0f && x > -22.0f)
		{
			return MakeWater(1, blend * DeepSurface);
		}

		if (x > 10.0f && x < 22.0f)
		{
			return MakeWater(2, (1.0f - blend) * DeepSurface);
		}

		return Dry();
	});

	f.Run(source, 2.0f);

	auto voices = f.ambience->GetVoices();
	REQUIRE(voices.size() == 1);
	CHECK(voices[0].soundEntryId == CalmEntry);
	float lastX = voices[0].position.x;
	CHECK(lastX < 0.0f);

	float largestStep = 0.0f;
	constexpr int SwapFrames = 160;
	for (int frame = 0; frame < SwapFrames + 60; ++frame)
	{
		blend = std::min(1.0f, static_cast<float>(frame + 1) / SwapFrames);
		f.ambience->Update(source, Vector3::Zero, false, 0, FrameSeconds);

		voices = f.ambience->GetVoices();
		REQUIRE(voices.size() == 1);
		largestStep = std::max(largestStep, std::abs(voices[0].position.x - lastX));
		lastX = voices[0].position.x;
	}

	CHECK(largestStep < 1.0f);
	CHECK(lastX > 0.0f);
	CHECK(std::count(f.audio.playedFiles.begin(), f.audio.playedFiles.end(), String(CalmFile)) == 1);
}

TEST_CASE("opposite shared beds glide through the ear and widen", "[water_ambience]")
{
	Fixture f;
	WaterAmbienceSounds left = RiverProfile();
	left.fastSound = 0;
	WaterAmbienceSounds right = left;
	right.profile.waterType = 2;
	f.ambience->SetProfiles({ left, right });

	// Two flat bands, mirror images of each other at the swap's midpoint. The left band
	// walks out of range while the right band walks in, so the voice crosses from left to
	// right with the candidates pointing exactly opposite halfway through.
	constexpr float Width = 12.0f;
	float blend = 0.0f;
	const FakeWaterSource source([&blend](const float x, float)
	{
		const float leftInner = 10.0f + 35.0f * blend;
		const float rightInner = 45.0f - 35.0f * blend;
		if (x < -leftInner && x > -leftInner - Width)
		{
			return MakeWater(1, 0.0f);
		}

		if (x > rightInner && x < rightInner + Width)
		{
			return MakeWater(2, 0.0f);
		}

		return Dry();
	});

	const Vector3 ear(0.0f, 1.6f, 0.0f);
	for (int frame = 0; frame < 40; ++frame)
	{
		f.ambience->Update(source, ear, false, 0, FrameSeconds);
	}

	auto voices = f.ambience->GetVoices();
	REQUIRE(voices.size() == 1);
	float lastX = voices[0].position.x;
	CHECK(lastX < 0.0f);

	float largestStep = 0.0f;
	bool crossedEar = false;
	constexpr int SwapFrames = 200;
	for (int frame = 0; frame < SwapFrames + 60; ++frame)
	{
		blend = std::min(1.0f, static_cast<float>(frame + 1) / SwapFrames);
		f.ambience->Update(source, ear, false, 0, FrameSeconds);

		voices = f.ambience->GetVoices();
		REQUIRE(voices.size() == 1);
		const float x = voices[0].position.x;
		largestStep = std::max(largestStep, std::abs(x - lastX));
		lastX = x;

		if (std::abs(x) < 0.5f)
		{
			crossedEar = true;
			CHECK(voices[0].spread > 250.0f);
		}
	}

	CHECK(crossedEar);
	CHECK(largestStep < 1.0f);
	CHECK(lastX > 0.0f);
}

TEST_CASE("no profiles means no work", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({});

	const FakeWaterSource source([](float, float)
	{
		return MakeWater(1, 0.0f, 0.5f);
	});

	f.Run(source, 1.0f, true, 1);

	CHECK(f.audio.startedChannels.empty());
	CHECK(f.ambience->GetVoices().empty());
}

TEST_CASE("destroying the ambience stops its voices", "[water_ambience]")
{
	Fixture f;
	f.ambience->SetProfiles({ RiverProfile() });

	const FakeWaterSource source([](float, float)
	{
		return MakeWater(1, 0.0f);
	});

	f.Run(source, 0.5f, true, 1);
	REQUIRE_FALSE(f.audio.startedChannels.empty());

	f.ambience.reset();
	CHECK(f.audio.stoppedChannels.size() == f.audio.startedChannels.size());
}
