// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "audio/audio.h"

namespace mmo
{
	/// @brief Fake channel instance which simply records the pitch and volume set on it.
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

	/// @brief Headless IAudio double: hands out incrementing sound / channel indices and
	///	records which channels were started and stopped, and which files were played.
	class FakeAudio final : public IAudio
	{
	public:
		/// Channels handed out by PlaySound / PlaySound3D, in order.
		std::vector<ChannelIndex> startedChannels;

		/// Channels passed to StopSound.
		std::set<ChannelIndex> stoppedChannels;

		/// File names behind every played sound index, in order.
		std::vector<String> playedFiles;

		/// Length in seconds reported by GetSoundLength for every sound index.
		float soundLength = 0.0f;

		void Create() override
		{
		}

		void Destroy() override
		{
		}

		void Update(const Vector3&, float) override
		{
		}

		SoundIndex CreateSound(const String& fileName) override
		{
			const SoundIndex index = m_nextSound++;
			m_files[index] = fileName;
			return index;
		}

		SoundIndex CreateStream(const String& fileName) override
		{
			return CreateSound(fileName);
		}

		SoundIndex CreateLoopedSound(const String& fileName) override
		{
			return CreateSound(fileName);
		}

		SoundIndex CreateLoopedStream(const String& fileName) override
		{
			return CreateSound(fileName);
		}

		SoundIndex CreateSound(const String& fileName, SoundType) override
		{
			return CreateSound(fileName);
		}

		void PlaySound(const SoundIndex sound, ChannelIndex* channelIndex, float, SoundCategory) override
		{
			Start(sound, channelIndex);
		}

		void PlaySound3D(const SoundIndex sound, ChannelIndex* channelIndex, const Vector3&, float, float, float, SoundCategory) override
		{
			Start(sound, channelIndex);
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
			return soundLength;
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

		/// @brief Gets the volume last set on the given channel.
		float GetChannelVolume(const ChannelIndex channel)
		{
			return m_channels[channel].GetVolume();
		}

		/// @brief Whether a sound created from the given file was played at least once.
		bool PlayedFile(const String& fileName) const
		{
			return std::find(playedFiles.begin(), playedFiles.end(), fileName) != playedFiles.end();
		}

	private:
		void Start(const SoundIndex sound, ChannelIndex* channelIndex)
		{
			const auto it = m_files.find(sound);
			playedFiles.push_back(it == m_files.end() ? String() : it->second);

			const ChannelIndex channel = m_nextChannel++;
			startedChannels.push_back(channel);

			if (channelIndex)
			{
				*channelIndex = channel;
			}
		}

	private:
		SoundIndex m_nextSound = 0;
		ChannelIndex m_nextChannel = 0;
		std::map<SoundIndex, String> m_files;
		std::map<ChannelIndex, FakeChannelInstance> m_channels;
	};
}
