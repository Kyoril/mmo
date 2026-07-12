// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include "shared/audio/audio.h"
#include "client_data/project.h"

#include <unordered_map>
#include <vector>

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

		/// @brief Gets the configured base volume [0, 1] of an entry (1.0 if unknown).
		float GetEntryVolume(uint32 soundId) const;

		/// @brief Gets the configured fade-in duration of an entry in seconds (0 if unknown).
		float GetEntryFadeInSeconds(uint32 soundId) const;

		/// @brief Gets the configured fade-out duration of an entry in seconds (0 if unknown).
		float GetEntryFadeOutSeconds(uint32 soundId) const;

	private:
		/// @brief Resolves the playback SoundType from the entry flags.
		static SoundType GetSoundTypeFromEntry(const proto_client::SoundEntry& entry);

		/// @brief Resolves the audio category from the entry category value.
		static SoundCategory GetCategoryFromEntry(const proto_client::SoundEntry& entry);

		/// @brief Loads a randomly picked file of the entry, or InvalidSound. Multi-file
		/// entries are picked shuffle-bag style (see NextShuffledFileIndex).
		SoundIndex LoadRandomFile(const proto_client::SoundEntry& entry);

		/// @brief Picks the next file index of a multi-file entry. Every file plays exactly
		/// once (in random order) before the bag reshuffles, and a new round never starts
		/// with the file that just played, so playback never repeats a file back-to-back.
		int NextShuffledFileIndex(const proto_client::SoundEntry& entry);

		/// @brief Applies volume and random pitch settings to a playing channel.
		void ApplyChannelSettings(ChannelIndex channel, const proto_client::SoundEntry& entry) const;

	private:
		/// @brief Shuffle-bag state of one multi-file sound entry.
		struct ShuffleState
		{
			/// Randomized file indices of the current round.
			std::vector<int> order;
			/// Next position in order to play.
			size_t next = 0;
			/// File index played most recently (-1 = none yet).
			int lastPlayed = -1;
		};

	private:
		IAudio& m_audio;
		const proto_client::SoundManager& m_sounds;
		std::unordered_map<uint32, ShuffleState> m_shuffleStates;
	};

	/// @brief Manages a long-running looped sound slot (zone music, zone ambience) which
	/// smoothly crossfades whenever a different sound entry is requested.
	///
	/// When the requested entry changes, the currently playing sound fades out (over its
	/// own fade_out_ms) while the new one simultaneously fades in (over its fade_in_ms), so
	/// the two overlap for a true crossfade rather than a hard cut. Multiple outgoing sounds
	/// can be fading out at once (e.g. rapid zone changes).
	///
	/// Call SetSound whenever the desired entry changes (comparison is by entry id, never by
	/// channel, as audio channels are recycled), and Update once per frame to advance the fades.
	class CrossfadingSoundLoop final : public NonCopyable
	{
	public:
		explicit CrossfadingSoundLoop(SoundEntryPlayer& player, IAudio& audio);
		~CrossfadingSoundLoop();

	public:
		/// @brief Requests the given sound entry to play in this slot (0 = fade out to silence).
		/// If the entry is still fading out from an earlier transition, that copy is faded
		/// back in without restarting playback, so rapid back-and-forth changes stay smooth.
		void SetSound(uint32 soundId);

		/// @brief Advances the running fades, if any.
		void Update(float deltaSeconds);

		/// @brief Immediately stops all playback (active and outgoing) without fading.
		void Stop();

		/// @brief Gets the id of the sound entry currently fading in / playing (0 = none).
		[[nodiscard]] uint32 GetCurrentSoundId() const { return m_activeSoundId; }

	private:
		/// @brief A sound that is currently fading out towards silence.
		struct FadingChannel
		{
			ChannelIndex channel = InvalidChannel;
			uint32 soundId = 0;
			float baseVolume = 1.0f;
			/// Normalized fade in [0, 1] (1 = full volume, 0 = silent).
			float fade = 1.0f;
			/// Normalized fade change per second (1 / fadeOutSeconds); 0 = never (instant handled separately).
			float fadeOutPerSec = 0.0f;
		};

		/// @brief Moves the current active sound (if any) into the fade-out list.
		void RetireActiveSound();

	private:
		SoundEntryPlayer& m_player;
		IAudio& m_audio;

		// Currently active (fading in / playing) sound.
		ChannelIndex m_activeChannel = InvalidChannel;
		uint32 m_activeSoundId = 0;
		float m_activeBaseVolume = 1.0f;
		/// Normalized fade in [0, 1] of the active sound.
		float m_activeFade = 0.0f;
		/// Normalized fade change per second while fading in (1 / fadeInSeconds); 0 = instant.
		float m_activeFadeInPerSec = 0.0f;

		std::vector<FadingChannel> m_fadingOut;
	};
}
