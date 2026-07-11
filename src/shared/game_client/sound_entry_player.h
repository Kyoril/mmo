// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include "shared/audio/audio.h"
#include "client_data/project.h"

namespace mmo
{
	/// @brief Plays SoundEntry data records (ClientDB sounds) through the audio system.
	///
	/// A sound entry describes one or more sound files together with playback settings
	/// (category, 2D/3D, looping, streaming, volume, pitch variation, attenuation).
	/// Systems that reference sounds by id (zone music, race voice lines, server-triggered
	/// sounds) use this class instead of talking to IAudio directly.
	class SoundEntryPlayer final : public NonCopyable
	{
	public:
		explicit SoundEntryPlayer(IAudio& audio, const proto_client::SoundManager& sounds);

	public:
		/// @brief Plays a sound entry as a global (2D) sound. 3D entries are played
		/// unpositioned as well, so prefer the positional overload for those.
		/// @return The channel the sound plays on, or InvalidChannel.
		ChannelIndex PlayEntry(uint32 soundId);

		/// @brief Plays a sound entry at a world position. Non-looped 3D sounds out of
		/// hearing range are not started at all.
		/// @return The channel the sound plays on, or InvalidChannel.
		ChannelIndex PlayEntry(uint32 soundId, const Vector3& position);

		/// @brief Gets the length of the (first) sound file of an entry in seconds.
		float GetEntryLength(uint32 soundId) const;

	private:
		/// @brief Resolves the playback SoundType from the entry flags.
		static SoundType GetSoundTypeFromEntry(const proto_client::SoundEntry& entry);

		/// @brief Resolves the audio category from the entry category value.
		static SoundCategory GetCategoryFromEntry(const proto_client::SoundEntry& entry);

		/// @brief Loads a randomly picked file of the entry, or InvalidSound.
		SoundIndex LoadRandomFile(const proto_client::SoundEntry& entry) const;

		/// @brief Applies volume and random pitch settings to a playing channel.
		void ApplyChannelSettings(ChannelIndex channel, const proto_client::SoundEntry& entry) const;

	private:
		IAudio& m_audio;
		const proto_client::SoundManager& m_sounds;
	};

	/// @brief Manages a single long-running looped sound slot (zone music, zone ambience)
	/// which smoothly crossfades whenever a different sound entry is requested.
	///
	/// Call SetSound whenever the desired entry changes (comparison is by entry id, never
	/// by channel, as audio channels are recycled), and Update once per frame to advance
	/// the fades.
	class CrossfadingSoundLoop final : public NonCopyable
	{
	public:
		explicit CrossfadingSoundLoop(SoundEntryPlayer& player, IAudio& audio, float fadeSpeed = 1.0f);
		~CrossfadingSoundLoop();

	public:
		/// @brief Requests the given sound entry to play in this slot (0 = fade out to silence).
		void SetSound(uint32 soundId);

		/// @brief Advances the running fade, if any.
		void Update(float deltaSeconds);

		/// @brief Immediately stops playback without fading.
		void Stop();

		/// @brief Gets the id of the sound entry that is playing or fading in/out right now.
		[[nodiscard]] uint32 GetCurrentSoundId() const { return m_currentSoundId; }

	private:
		/// @brief Starts the pending entry at zero volume and begins the fade in.
		void StartPendingSound();

	private:
		enum class State : uint8
		{
			Idle,
			FadingIn,
			FadingOut
		};

		SoundEntryPlayer& m_player;
		IAudio& m_audio;
		float m_fadeSpeed;

		uint32 m_currentSoundId = 0;
		uint32 m_pendingSoundId = 0;
		ChannelIndex m_channel = InvalidChannel;
		float m_baseVolume = 1.0f;
		float m_fade = 0.0f;
		State m_state = State::Idle;
	};
}
