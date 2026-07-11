// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "sound_entry_player.h"

#include "log/default_log_levels.h"

#include <algorithm>
#include <numeric>
#include <random>

namespace mmo
{
	namespace
	{
		std::mt19937& GetRandomGenerator()
		{
			static std::random_device rd;
			static std::mt19937 gen(rd());
			return gen;
		}
	}

	SoundEntryPlayer::SoundEntryPlayer(IAudio& audio, const proto_client::SoundManager& sounds)
		: m_audio(audio)
		, m_sounds(sounds)
	{
	}

	ChannelIndex SoundEntryPlayer::PlayEntry(const uint32 soundId)
	{
		const proto_client::SoundEntry* entry = m_sounds.getById(soundId);
		if (!entry)
		{
			WLOG("Unable to play sound entry " << soundId << ": unknown id");
			return InvalidChannel;
		}

		const SoundIndex sound = LoadRandomFile(*entry);
		if (sound == InvalidSound)
		{
			return InvalidChannel;
		}

		ChannelIndex channel = InvalidChannel;
		m_audio.PlaySound(sound, &channel, 1.0f, GetCategoryFromEntry(*entry));
		ApplyChannelSettings(channel, *entry);
		return channel;
	}

	ChannelIndex SoundEntryPlayer::PlayEntry(const uint32 soundId, const Vector3& position)
	{
		const proto_client::SoundEntry* entry = m_sounds.getById(soundId);
		if (!entry)
		{
			WLOG("Unable to play sound entry " << soundId << ": unknown id");
			return InvalidChannel;
		}

		const SoundIndex sound = LoadRandomFile(*entry);
		if (sound == InvalidSound)
		{
			return InvalidChannel;
		}

		ChannelIndex channel = InvalidChannel;
		if (entry->is_3d())
		{
			m_audio.PlaySound3D(sound, &channel, position, entry->min_distance(), entry->max_distance(), 1.0f, GetCategoryFromEntry(*entry));
		}
		else
		{
			m_audio.PlaySound(sound, &channel, 1.0f, GetCategoryFromEntry(*entry));
		}

		ApplyChannelSettings(channel, *entry);
		return channel;
	}

	float SoundEntryPlayer::GetEntryLength(const uint32 soundId) const
	{
		const proto_client::SoundEntry* entry = m_sounds.getById(soundId);
		if (!entry || entry->files_size() == 0)
		{
			return 0.0f;
		}

		const SoundType type = GetSoundTypeFromEntry(*entry);
		SoundIndex sound = m_audio.FindSound(entry->files(0), type);
		if (sound == InvalidSound)
		{
			sound = m_audio.CreateSound(entry->files(0), type);
		}

		if (sound == InvalidSound)
		{
			return 0.0f;
		}

		return m_audio.GetSoundLength(sound);
	}

	float SoundEntryPlayer::GetEntryVolume(const uint32 soundId) const
	{
		const proto_client::SoundEntry* entry = m_sounds.getById(soundId);
		return entry ? entry->volume() : 1.0f;
	}

	float SoundEntryPlayer::GetEntryFadeInSeconds(const uint32 soundId) const
	{
		const proto_client::SoundEntry* entry = m_sounds.getById(soundId);
		return entry ? entry->fade_in_ms() / 1000.0f : 0.0f;
	}

	float SoundEntryPlayer::GetEntryFadeOutSeconds(const uint32 soundId) const
	{
		const proto_client::SoundEntry* entry = m_sounds.getById(soundId);
		return entry ? entry->fade_out_ms() / 1000.0f : 0.0f;
	}

	SoundType SoundEntryPlayer::GetSoundTypeFromEntry(const proto_client::SoundEntry& entry)
	{
		if (entry.is_3d())
		{
			return entry.looped() ? SoundType::SoundLooped3D : SoundType::Sound3D;
		}

		// 2D streams are loaded as looped 2D streams when looping is requested; otherwise
		// a plain 2D sound is sufficient (CreateStream also maps to Sound2D).
		return entry.looped() ? SoundType::SoundLooped2D : SoundType::Sound2D;
	}

	SoundCategory SoundEntryPlayer::GetCategoryFromEntry(const proto_client::SoundEntry& entry)
	{
		// The proto enum values are kept in sync with mmo::SoundCategory by definition.
		const auto value = static_cast<uint8>(entry.category());
		if (value >= static_cast<uint8>(SoundCategory::Count_))
		{
			return SoundCategory::SoundEffects;
		}

		return static_cast<SoundCategory>(value);
	}

	SoundIndex SoundEntryPlayer::LoadRandomFile(const proto_client::SoundEntry& entry)
	{
		if (entry.files_size() == 0)
		{
			WLOG("Sound entry " << entry.id() << " (" << entry.name() << ") has no files");
			return InvalidSound;
		}

		int fileIndex = 0;
		if (entry.files_size() > 1)
		{
			fileIndex = NextShuffledFileIndex(entry);
		}

		const String& file = entry.files(fileIndex);
		const SoundType type = GetSoundTypeFromEntry(entry);

		SoundIndex sound = m_audio.FindSound(file, type);
		if (sound == InvalidSound)
		{
			sound = m_audio.CreateSound(file, type);
		}

		return sound;
	}

	int SoundEntryPlayer::NextShuffledFileIndex(const proto_client::SoundEntry& entry)
	{
		ShuffleState& state = m_shuffleStates[entry.id()];

		// (Re)build the bag when the round is exhausted (also covers a changed file count,
		// e.g. after a data reload, since order is resized to match).
		if (state.next >= state.order.size() || state.order.size() != static_cast<size_t>(entry.files_size()))
		{
			state.order.resize(entry.files_size());
			std::iota(state.order.begin(), state.order.end(), 0);
			std::shuffle(state.order.begin(), state.order.end(), GetRandomGenerator());

			// A new round must not start with the file that just ended the previous one,
			// so no file ever plays twice in a row.
			if (state.order.size() > 1 && state.order.front() == state.lastPlayed)
			{
				std::uniform_int_distribution<size_t> dis(1, state.order.size() - 1);
				std::swap(state.order.front(), state.order[dis(GetRandomGenerator())]);
			}

			state.next = 0;
		}

		state.lastPlayed = state.order[state.next++];
		return state.lastPlayed;
	}

	void SoundEntryPlayer::ApplyChannelSettings(const ChannelIndex channel, const proto_client::SoundEntry& entry) const
	{
		if (channel == InvalidChannel)
		{
			return;
		}

		IChannelInstance* instance = m_audio.GetChannelInstance(channel);
		if (!instance)
		{
			return;
		}

		instance->SetVolume(entry.volume());

		if (entry.pitch_min() < entry.pitch_max())
		{
			std::uniform_real_distribution<float> pitchDistribution(entry.pitch_min(), entry.pitch_max());
			instance->SetPitch(pitchDistribution(GetRandomGenerator()));
		}
		else if (entry.pitch_min() != 1.0f)
		{
			instance->SetPitch(entry.pitch_min());
		}
	}


	CrossfadingSoundLoop::CrossfadingSoundLoop(SoundEntryPlayer& player, IAudio& audio)
		: m_player(player)
		, m_audio(audio)
	{
	}

	CrossfadingSoundLoop::~CrossfadingSoundLoop()
	{
		Stop();
	}

	void CrossfadingSoundLoop::SetSound(const uint32 soundId)
	{
		// Already the active (playing / fading-in) sound: nothing to do.
		if (soundId == m_activeSoundId)
		{
			return;
		}

		// Fade out whatever is currently active.
		RetireActiveSound();

		// A request for "no sound" just leaves the fade-outs running.
		if (soundId == 0)
		{
			return;
		}

		// Avoid the same track playing twice out of phase: if this sound is still fading
		// out from a previous transition, stop that copy before starting a fresh one.
		for (auto it = m_fadingOut.begin(); it != m_fadingOut.end();)
		{
			if (it->soundId == soundId)
			{
				m_audio.StopSound(&it->channel);
				it = m_fadingOut.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Start the new sound and fade it in from silence.
		m_activeChannel = m_player.PlayEntry(soundId);
		if (m_activeChannel == InvalidChannel)
		{
			m_activeSoundId = 0;
			return;
		}

		m_activeSoundId = soundId;
		m_activeBaseVolume = m_player.GetEntryVolume(soundId);

		const float fadeInSeconds = m_player.GetEntryFadeInSeconds(soundId);
		if (fadeInSeconds > 0.0f)
		{
			m_activeFade = 0.0f;
			m_activeFadeInPerSec = 1.0f / fadeInSeconds;
			if (IChannelInstance* instance = m_audio.GetChannelInstance(m_activeChannel))
			{
				instance->SetVolume(0.0f);
			}
		}
		else
		{
			// Instant: PlayEntry already applied the base volume.
			m_activeFade = 1.0f;
			m_activeFadeInPerSec = 0.0f;
		}
	}

	void CrossfadingSoundLoop::Update(const float deltaSeconds)
	{
		// Advance the active fade-in.
		if (m_activeChannel != InvalidChannel && m_activeFadeInPerSec > 0.0f && m_activeFade < 1.0f)
		{
			m_activeFade += m_activeFadeInPerSec * deltaSeconds;
			if (m_activeFade >= 1.0f)
			{
				m_activeFade = 1.0f;
				m_activeFadeInPerSec = 0.0f;
			}

			if (IChannelInstance* instance = m_audio.GetChannelInstance(m_activeChannel))
			{
				instance->SetVolume(m_activeFade * m_activeBaseVolume);
			}
		}

		// Advance every fade-out, dropping the ones that reached silence.
		for (auto it = m_fadingOut.begin(); it != m_fadingOut.end();)
		{
			it->fade -= it->fadeOutPerSec * deltaSeconds;
			if (it->fade <= 0.0f)
			{
				m_audio.StopSound(&it->channel);
				it = m_fadingOut.erase(it);
				continue;
			}

			if (IChannelInstance* instance = m_audio.GetChannelInstance(it->channel))
			{
				instance->SetVolume(it->fade * it->baseVolume);
			}
			++it;
		}
	}

	void CrossfadingSoundLoop::Stop()
	{
		if (m_activeChannel != InvalidChannel)
		{
			m_audio.StopSound(&m_activeChannel);
		}
		m_activeChannel = InvalidChannel;
		m_activeSoundId = 0;
		m_activeFade = 0.0f;
		m_activeFadeInPerSec = 0.0f;

		for (auto& fading : m_fadingOut)
		{
			m_audio.StopSound(&fading.channel);
		}
		m_fadingOut.clear();
	}

	void CrossfadingSoundLoop::RetireActiveSound()
	{
		if (m_activeChannel == InvalidChannel)
		{
			m_activeSoundId = 0;
			return;
		}

		const float fadeOutSeconds = m_player.GetEntryFadeOutSeconds(m_activeSoundId);
		if (fadeOutSeconds <= 0.0f)
		{
			// Instant stop.
			m_audio.StopSound(&m_activeChannel);
		}
		else
		{
			FadingChannel fading;
			fading.channel = m_activeChannel;
			fading.soundId = m_activeSoundId;
			fading.baseVolume = m_activeBaseVolume;
			// Continue fading from the volume the sound had actually reached so far.
			fading.fade = m_activeFade;
			fading.fadeOutPerSec = 1.0f / fadeOutSeconds;
			m_fadingOut.push_back(fading);
		}

		m_activeChannel = InvalidChannel;
		m_activeSoundId = 0;
		m_activeFade = 0.0f;
		m_activeFadeInPerSec = 0.0f;
	}
}
