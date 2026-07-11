// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "sound_entry_player.h"

#include "log/default_log_levels.h"

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

	SoundIndex SoundEntryPlayer::LoadRandomFile(const proto_client::SoundEntry& entry) const
	{
		if (entry.files_size() == 0)
		{
			WLOG("Sound entry " << entry.id() << " (" << entry.name() << ") has no files");
			return InvalidSound;
		}

		int fileIndex = 0;
		if (entry.files_size() > 1)
		{
			std::uniform_int_distribution<int> dis(0, entry.files_size() - 1);
			fileIndex = dis(GetRandomGenerator());
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


	CrossfadingSoundLoop::CrossfadingSoundLoop(SoundEntryPlayer& player, IAudio& audio, const float fadeSpeed)
		: m_player(player)
		, m_audio(audio)
		, m_fadeSpeed(fadeSpeed)
	{
	}

	CrossfadingSoundLoop::~CrossfadingSoundLoop()
	{
		Stop();
	}

	void CrossfadingSoundLoop::SetSound(const uint32 soundId)
	{
		// Already playing or fading towards this entry?
		if (soundId == m_currentSoundId && m_state != State::FadingOut)
		{
			return;
		}

		// Re-requesting the entry that is currently fading out: fade it back in.
		if (soundId == m_currentSoundId && m_state == State::FadingOut)
		{
			m_pendingSoundId = 0;
			m_state = State::FadingIn;
			return;
		}

		m_pendingSoundId = soundId;

		if (m_channel != InvalidChannel)
		{
			m_state = State::FadingOut;
		}
		else
		{
			StartPendingSound();
		}
	}

	void CrossfadingSoundLoop::Update(const float deltaSeconds)
	{
		if (m_state == State::Idle || m_channel == InvalidChannel)
		{
			return;
		}

		IChannelInstance* instance = m_audio.GetChannelInstance(m_channel);

		if (m_state == State::FadingOut)
		{
			m_fade -= m_fadeSpeed * deltaSeconds;
			if (m_fade <= 0.0f)
			{
				m_audio.StopSound(&m_channel);
				m_currentSoundId = 0;
				m_fade = 0.0f;
				m_state = State::Idle;

				StartPendingSound();
				return;
			}

			if (instance)
			{
				instance->SetVolume(m_fade * m_baseVolume);
			}
		}
		else if (m_state == State::FadingIn)
		{
			m_fade += m_fadeSpeed * deltaSeconds;
			if (m_fade >= 1.0f)
			{
				m_fade = 1.0f;
				m_state = State::Idle;
			}

			if (instance)
			{
				instance->SetVolume(m_fade * m_baseVolume);
			}
		}
	}

	void CrossfadingSoundLoop::Stop()
	{
		if (m_channel != InvalidChannel)
		{
			m_audio.StopSound(&m_channel);
		}

		m_currentSoundId = 0;
		m_pendingSoundId = 0;
		m_fade = 0.0f;
		m_state = State::Idle;
	}

	void CrossfadingSoundLoop::StartPendingSound()
	{
		if (m_pendingSoundId == 0)
		{
			return;
		}

		const uint32 soundId = m_pendingSoundId;
		m_pendingSoundId = 0;

		m_channel = m_player.PlayEntry(soundId);
		if (m_channel == InvalidChannel)
		{
			return;
		}

		m_currentSoundId = soundId;

		// PlayEntry applied the entry's base volume to the channel; remember it as the fade
		// target and restart playback silently.
		if (IChannelInstance* instance = m_audio.GetChannelInstance(m_channel))
		{
			m_baseVolume = instance->GetVolume();
			instance->SetVolume(0.0f);
		}
		else
		{
			m_baseVolume = 1.0f;
		}

		m_fade = 0.0f;
		m_state = State::FadingIn;
	}
}
