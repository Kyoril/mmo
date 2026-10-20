// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include "shared/audio/audio.h"

#include <map>
#include <random>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mmo
{
	class SoundEntryPlayer;

	/// @brief A looping sound at a fixed world position, e.g. a placed torch's crackle.
	struct AmbientSoundSource
	{
		/// @brief Id of the SoundEntry to play. Must be a looped 3D entry.
		uint32 soundEntryId = 0;

		/// @brief World position.
		Vector3 position = Vector3::Zero;

		/// @brief Multiplier on the entry's volume.
		float volume = 1.0f;

		/// @brief Audible range override in metres. 0 uses the entry's max distance.
		float maxDistance = 0.0f;
	};

	namespace ambient_sound_owner
	{
		/// @brief What placed a group of ambient sound sources. Each kind has its own id space.
		enum Type : uint8
		{
			/// @brief A plain mesh placement.
			MapEntity,

			/// @brief A world model placement, including its doodads, kit props and children.
			WorldModel
		};
	}

	/// @brief An ambient sound source that is within its audible range of the listener.
	struct AmbientVoiceCandidate
	{
		/// @brief Caller-defined identity, returned by the selection.
		uint64 key = 0;

		/// @brief Sound entry, for the per-entry voice cap.
		uint32 soundEntryId = 0;

		/// @brief Squared distance to the listener.
		float distanceSq = 0.0f;
	};

	/// @brief Picks the candidates that get a voice: nearest first, at most maxVoices in total and
	///        at most maxPerEntry per sound entry, so a hall of identical torches plays only the
	///        closest few. Ties keep their input order.
	/// @return The keys of the selected candidates, nearest first.
	[[nodiscard]] std::vector<uint64> SelectAmbientVoices(std::span<const AmbientVoiceCandidate> candidates, size_t maxVoices, size_t maxPerEntry);

	/// @brief Plays the looping sounds of placed meshes that are near the listener.
	/// @details The world registers the sources of each placement as it streams in and removes them
	///          as it streams out. Update re-selects the audible sources a few times per second and
	///          fades voices in and out as they enter or leave the selection. Voices start at a
	///          random point of their loop so that identical sources never play in lockstep.
	class AmbientSoundEmitters final : public NonCopyable
	{
	public:
		/// @brief Most voices playing at once, out of the audio system's 64 channels.
		static constexpr size_t MaxVoices = 12;

		/// @brief Most voices of one sound entry playing at once.
		static constexpr size_t MaxVoicesPerEntry = 4;

		/// @brief Seconds between two selections.
		static constexpr float ReselectInterval = 0.25f;

		/// @brief A playing voice keeps its slot until the listener is this much beyond its range.
		static constexpr float KeepRangeFactor = 1.1f;

		/// @brief Shortest fade in seconds; voices swapped by the cap change within earshot.
		static constexpr float MinFadeSeconds = 0.3f;

	public:
		explicit AmbientSoundEmitters(SoundEntryPlayer& player, IAudio& audio, uint32 randomSeed = std::random_device{}());
		~AmbientSoundEmitters();

	public:
		/// @brief Registers the sources of one placement. Replaces what the owner had registered.
		void Add(ambient_sound_owner::Type kind, uint64 ownerId, std::vector<AmbientSoundSource> sources);

		/// @brief Removes the sources of one placement. Their voices fade out.
		void Remove(ambient_sound_owner::Type kind, uint64 ownerId);

		/// @brief Removes every source and stops every voice at once.
		void Clear();

		/// @brief Advances fades and, every ReselectInterval, re-selects the audible sources.
		/// @param listener The listener's world position.
		/// @param deltaSeconds Seconds since the last update.
		void Update(const Vector3& listener, float deltaSeconds);

		/// @brief Number of registered sources.
		[[nodiscard]] size_t GetSourceCount() const { return m_sources.size(); }

		/// @brief Number of voices that are playing or fading in (not those fading out).
		[[nodiscard]] size_t GetActiveVoiceCount() const;

	private:
		using OwnerKey = std::pair<uint8, uint64>;

		struct Voice
		{
			uint64 sourceId = 0;
			uint32 soundEntryId = 0;
			ChannelIndex channel = InvalidChannel;
			float baseVolume = 1.0f;
			/// Normalized fade in [0, 1].
			float fade = 0.0f;
			float fadeInPerSec = 0.0f;
			float fadeOutPerSec = 0.0f;
			bool retiring = false;
		};

		void Reselect(const Vector3& listener);

		/// @brief Starts a voice for a source, or revives its voice if that is still fading out.
		void StartVoice(uint64 sourceId, const AmbientSoundSource& source);

		/// @brief Whether the entry can play as an ambient loop; warns once per entry if not.
		bool IsPlayableEntry(uint32 soundEntryId);

		void ApplyVolume(const Voice& voice) const;

	private:
		SoundEntryPlayer& m_player;
		IAudio& m_audio;
		std::mt19937 m_random;

		uint64 m_nextSourceId = 1;
		std::unordered_map<uint64, AmbientSoundSource> m_sources;
		std::map<OwnerKey, std::vector<uint64>> m_owners;

		std::vector<Voice> m_voices;
		float m_reselectTimer = 0.0f;

		std::unordered_set<uint32> m_rejectedEntries;
	};
}
