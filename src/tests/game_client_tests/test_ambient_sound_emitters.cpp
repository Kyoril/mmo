// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_audio.h"

#include "game_client/ambient_sound_emitters.h"
#include "game_client/sound_entry_player.h"

using namespace mmo;

namespace
{
	proto_client::SoundEntry& AddAmbientEntry(proto_client::SoundManager& sounds, const uint32 id, const float maxDistance)
	{
		auto* entry = sounds.add(id);
		REQUIRE(entry != nullptr);
		entry->add_files("Sound/Ambience/test_" + std::to_string(id) + ".wav");
		entry->set_is_3d(true);
		entry->set_looped(true);
		entry->set_volume(0.8f);
		entry->set_max_distance(maxDistance);
		return *entry;
	}

	AmbientSoundSource Source(const uint32 soundId, const Vector3& position)
	{
		AmbientSoundSource source;
		source.soundEntryId = soundId;
		source.position = position;
		return source;
	}

	struct Fixture
	{
		FakeAudio audio;
		proto_client::SoundManager sounds;
		std::unique_ptr<SoundEntryPlayer> player;
		std::unique_ptr<AmbientSoundEmitters> emitters;

		Fixture()
		{
			AddAmbientEntry(sounds, 1, 10.0f);
			AddAmbientEntry(sounds, 2, 20.0f);
			audio.soundLength = 4.0f;
			player = std::make_unique<SoundEntryPlayer>(audio, sounds);
			emitters = std::make_unique<AmbientSoundEmitters>(*player, audio, 1234u);
		}
	};
}

TEST_CASE("SelectAmbientVoices keeps the nearest within both caps", "[ambient_sound]")
{
	const std::vector<AmbientVoiceCandidate> candidates = {
		{ 1, 7, 50.0f }, { 2, 7, 10.0f }, { 3, 7, 30.0f }, { 4, 9, 40.0f }, { 5, 7, 20.0f }
	};

	SECTION("per-entry cap")
	{
		const auto selected = SelectAmbientVoices(candidates, 10, 2);
		CHECK(selected == std::vector<uint64>{ 2, 5, 4 });
	}

	SECTION("total cap")
	{
		const auto selected = SelectAmbientVoices(candidates, 3, 10);
		CHECK(selected == std::vector<uint64>{ 2, 5, 3 });
	}
}

TEST_CASE("Ambient sources start in range and fade in", "[ambient_sound]")
{
	Fixture f;
	f.emitters->Add(ambient_sound_owner::MapEntity, 100, { Source(1, Vector3(5, 0, 0)), Source(1, Vector3(50, 0, 0)) });

	f.emitters->Update(Vector3::Zero, 0.0f);
	REQUIRE(f.audio.startedChannels.size() == 1);
	const ChannelIndex channel = f.audio.startedChannels[0];
	CHECK(f.audio.channelPositions[channel].x == Approx(5.0f));
	CHECK(f.audio.GetChannelVolume(channel) == Approx(0.0f));

	// Random loop start inside the sound's length
	CHECK(f.audio.GetChannelPlaybackPosition(channel) >= 0.0f);
	CHECK(f.audio.GetChannelPlaybackPosition(channel) < 4.0f);

	// Entry fades are 0, so the 0.3 s floor applies
	f.emitters->Update(Vector3::Zero, 0.15f);
	CHECK(f.audio.GetChannelVolume(channel) == Approx(0.4f));
	f.emitters->Update(Vector3::Zero, 0.15f);
	CHECK(f.audio.GetChannelVolume(channel) == Approx(0.8f));
}

TEST_CASE("Ambient voices keep playing slightly past range and stop beyond it", "[ambient_sound]")
{
	Fixture f;
	f.emitters->Add(ambient_sound_owner::MapEntity, 100, { Source(1, Vector3::Zero) });
	f.emitters->Update(Vector3(9, 0, 0), 0.0f);
	f.emitters->Update(Vector3(9, 0, 0), 1.0f);
	REQUIRE(f.emitters->GetActiveVoiceCount() == 1);

	// 10.5 m: outside the 10 m range, inside the 11 m keep range
	f.emitters->Update(Vector3(10.5f, 0, 0), AmbientSoundEmitters::ReselectInterval);
	CHECK(f.emitters->GetActiveVoiceCount() == 1);

	f.emitters->Update(Vector3(12, 0, 0), AmbientSoundEmitters::ReselectInterval);
	CHECK(f.emitters->GetActiveVoiceCount() == 0);
	CHECK(f.audio.stoppedChannels.empty());

	f.emitters->Update(Vector3(12, 0, 0), 0.5f);
	CHECK(f.audio.stoppedChannels.size() == 1);
}

TEST_CASE("Ambient sources honour the per-entry voice cap", "[ambient_sound]")
{
	Fixture f;
	std::vector<AmbientSoundSource> torches;
	for (int i = 0; i < 10; ++i)
	{
		torches.push_back(Source(1, Vector3(static_cast<float>(i), 0, 0)));
	}
	f.emitters->Add(ambient_sound_owner::WorldModel, 7, torches);
	f.emitters->Add(ambient_sound_owner::MapEntity, 7, { Source(2, Vector3(0, 0, 3)) });

	f.emitters->Update(Vector3::Zero, 0.0f);
	CHECK(f.emitters->GetActiveVoiceCount() == AmbientSoundEmitters::MaxVoicesPerEntry + 1);
	CHECK(f.emitters->GetSourceCount() == 11);
}

TEST_CASE("Removing an owner fades its voices out; the other kind with the same id stays", "[ambient_sound]")
{
	Fixture f;
	f.emitters->Add(ambient_sound_owner::WorldModel, 7, { Source(1, Vector3(1, 0, 0)) });
	f.emitters->Add(ambient_sound_owner::MapEntity, 7, { Source(2, Vector3(2, 0, 0)) });
	f.emitters->Update(Vector3::Zero, 0.0f);
	REQUIRE(f.emitters->GetActiveVoiceCount() == 2);

	f.emitters->Remove(ambient_sound_owner::WorldModel, 7);
	f.emitters->Update(Vector3::Zero, 0.0f);
	CHECK(f.emitters->GetActiveVoiceCount() == 1);
	CHECK(f.emitters->GetSourceCount() == 1);

	f.emitters->Update(Vector3::Zero, 1.0f);
	CHECK(f.audio.stoppedChannels.size() == 1);
}

TEST_CASE("A source returning while its voice fades out is faded back in, not restarted", "[ambient_sound]")
{
	Fixture f;
	f.emitters->Add(ambient_sound_owner::MapEntity, 1, { Source(1, Vector3::Zero) });
	f.emitters->Update(Vector3::Zero, 0.0f);
	f.emitters->Update(Vector3::Zero, 1.0f);

	f.emitters->Update(Vector3(20, 0, 0), AmbientSoundEmitters::ReselectInterval);
	CHECK(f.emitters->GetActiveVoiceCount() == 0);

	f.emitters->Update(Vector3::Zero, AmbientSoundEmitters::ReselectInterval);
	CHECK(f.emitters->GetActiveVoiceCount() == 1);
	CHECK(f.audio.startedChannels.size() == 1);
	CHECK(f.audio.stoppedChannels.empty());
}

TEST_CASE("Entries that are not looped 3D sounds stay silent", "[ambient_sound]")
{
	Fixture f;
	AddAmbientEntry(f.sounds, 3, 10.0f).set_looped(false);
	f.emitters->Add(ambient_sound_owner::MapEntity, 1, { Source(3, Vector3::Zero), Source(99, Vector3::Zero) });

	f.emitters->Update(Vector3::Zero, 0.0f);
	CHECK(f.audio.startedChannels.empty());
}

TEST_CASE("Clear stops every voice at once", "[ambient_sound]")
{
	Fixture f;
	f.emitters->Add(ambient_sound_owner::MapEntity, 1, { Source(1, Vector3::Zero), Source(2, Vector3(1, 0, 0)) });
	f.emitters->Update(Vector3::Zero, 0.0f);
	REQUIRE(f.audio.startedChannels.size() == 2);

	f.emitters->Clear();
	CHECK(f.audio.stoppedChannels.size() == 2);
	CHECK(f.emitters->GetSourceCount() == 0);
}
