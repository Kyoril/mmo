// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <random>
#include <set>
#include <vector>

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include "shared/audio/audio.h"
#include "game_client/water_ambience_scan.h"

namespace mmo
{
	class SoundEntryPlayer;

	/// @brief Sounds of one water ambience profile.
	struct WaterAmbienceSounds
	{
		/// @brief Which water the scan aggregates for these sounds.
		WaterAmbienceProfile profile;

		/// @brief Sound entry of the calm bed loop (0 = none).
		uint32 calmSound = 0;

		/// @brief Sound entry of the fast flowing bed loop (0 = none, the calm loop plays alone).
		uint32 fastSound = 0;

		/// @brief Sound entry of the close 2D layer while swimming in this water (0 = none).
		uint32 swimSound = 0;

		/// @brief Volume multiplier of every sound of this profile.
		float volume = 1.0f;
	};

	namespace water_ambience
	{
		/// @brief Seconds between two water scans.
		constexpr float ScanInterval = 0.25f;

		/// @brief Time constant of the one-pole filter on level, speed and direction, in seconds.
		constexpr float SmoothingSeconds = 0.5f;

		/// @brief Distance from the ear at which fully focused bed voices are placed (scaled by the
		///	focus, so surrounding water places the voice at the ear), and their 3D min distance.
		constexpr float VirtualDistance = 5.0f;

		/// @brief Seconds a bed voice must stay silent before it is stopped.
		constexpr float StopHoldSeconds = 2.0f;

		/// @brief Fade in and fade out time of the swim layer, in seconds.
		constexpr float SwimFadeSeconds = 0.5f;

		/// @brief 3D spread in degrees of a bed voice whose water lies in one direction.
		constexpr float MinSpread = 60.0f;

		/// @brief 3D spread in degrees of a bed voice whose water surrounds the ear.
		constexpr float MaxSpread = 360.0f;
	}

	/// @brief Plays the water ambience around the listener.
	///
	///	Scans the water around the ear every water_ambience::ScanInterval, low-passes the scan
	///	result and drives per profile a calm and a fast bed loop (equal-power crossfaded by flow
	///	speed), placed in the direction of the water and widened by 3D spread when the water
	///	surrounds the ear. While swimming, a 2D swim layer of the swum water fades in.
	class WaterAmbience final : public NonCopyable
	{
	public:
		/// @brief Creates the water ambience. Nothing plays until profiles are set.
		/// @param player Player used to start the sound entries.
		/// @param audio Audio system used to adjust the playing channels.
		/// @param randomSeed Seed of the random start positions within the loops.
		WaterAmbience(SoundEntryPlayer& player, IAudio& audio, uint32 randomSeed = std::random_device{}());

		/// @brief Stops all voices.
		~WaterAmbience();

	public:
		/// @brief Replaces the profiles. Stops everything that plays and resets the smoothing.
		/// @param profiles New profiles. An empty list disables the ambience.
		void SetProfiles(std::vector<WaterAmbienceSounds> profiles);

		/// @brief Advances scanning, smoothing and playback by one frame.
		/// @param source Source answering water samples for the scan.
		/// @param ear World position of the listener.
		/// @param swimming Whether the player currently swims.
		/// @param swimWaterType Water type the player swims in. Only used while swimming.
		/// @param deltaSeconds Seconds since the last update.
		void Update(const IWaterAmbienceSource& source, const Vector3& ear, bool swimming, uint32 swimWaterType, float deltaSeconds);

		/// @brief Immediately stops every voice and resets the smoothing.
		void Stop();

		/// @brief Gets the smoothed fields, one per profile in profile order.
		[[nodiscard]] const std::vector<WaterAmbienceField>& GetSmoothedFields() const;

		/// @brief Debug information about one live voice.
		struct VoiceInfo
		{
			/// @brief Sound entry the voice plays.
			uint32 soundEntryId;

			/// @brief Volume last set on the voice.
			float volume;

			/// @brief 3D spread in degrees last set on the voice.
			float spread;

			/// @brief World position last set on the voice.
			Vector3 position;
		};

		/// @brief Gets the live voices: the bed voices, then the swim layer if it plays.
		[[nodiscard]] std::vector<VoiceInfo> GetVoices() const;

	private:
		/// @brief A live bed loop.
		struct BedVoice
		{
			ChannelIndex channel = InvalidChannel;
			float volume = 0.0f;
			float spread = water_ambience::MaxSpread;
			Vector3 position;

			/// @brief Seconds the volume has stayed at or below the audible threshold.
			float silentSeconds = 0.0f;
		};

		/// @brief What one sound entry should play like this frame.
		struct BedTarget
		{
			float volume = 0.0f;
			float spread = water_ambience::MaxSpread;
			Vector3 position;
			float range = 0.0f;
		};

	private:
		/// @brief Low-passes the latest scan into the smoothed fields.
		void Smooth(float deltaSeconds);

		/// @brief Collects the target of every bed sound entry. Profiles sharing an entry are
		///	blended: volume-weighted direction, spread from the blended direction's length,
		///	equal-power summed volume.
		[[nodiscard]] std::map<uint32, BedTarget> GatherBedTargets(const Vector3& ear) const;

		/// @brief Starts, adjusts and stops the bed voices.
		void UpdateBedVoices(const Vector3& ear, float deltaSeconds);

		/// @brief Starts, fades and stops the swim layer.
		void UpdateSwimLayer(bool swimming, uint32 swimWaterType, float deltaSeconds);

		/// @brief Applies volume, position and spread to a channel.
		void ApplyBedVoice(const BedVoice& voice) const;

		/// @brief Stops the swim layer immediately.
		void StopSwimLayer();

	private:
		SoundEntryPlayer& m_player;
		IAudio& m_audio;
		std::mt19937 m_random;

		std::vector<WaterAmbienceSounds> m_profiles;
		std::vector<WaterAmbienceProfile> m_scanProfiles;

		/// @brief Latest raw scan result, one per profile.
		std::vector<WaterAmbienceField> m_targetFields;

		/// @brief Low-passed fields, one per profile.
		std::vector<WaterAmbienceField> m_smoothedFields;

		/// @brief Whether a smoothed field has ever seen water and thus owns a direction.
		std::vector<bool> m_hasDirection;

		/// @brief Seconds until the next scan.
		float m_scanCountdown = 0.0f;

		/// @brief Whether the next update must scan regardless of the countdown.
		bool m_scanDue = true;

		/// @brief Live bed voices by sound entry id.
		std::map<uint32, BedVoice> m_bedVoices;

		ChannelIndex m_swimChannel = InvalidChannel;
		uint32 m_swimSoundId = 0;
		float m_swimBaseVolume = 1.0f;

		/// @brief Normalized fade of the swim layer in [0, 1].
		float m_swimFade = 0.0f;

		/// @brief Sound entries which failed to start. They are not retried until the profiles
		///	change, so a broken entry does not log a warning every frame.
		std::set<uint32> m_failedEntries;

		Vector3 m_lastEar;
	};
}
