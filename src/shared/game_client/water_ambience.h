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

		/// @brief Sound entry of the 2D layer while the head is under this water; replaces the
		///	swim layer while it plays (0 = none, the swim layer continues).
		uint32 underwaterSound = 0;

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

		/// @brief Fade in and fade out time of the underwater layer, in seconds.
		constexpr float UnderwaterFadeSeconds = 0.4f;

		/// @brief 3D spread in degrees of a bed voice whose water lies in one direction.
		constexpr float MinSpread = 60.0f;

		/// @brief 3D spread in degrees of a bed voice whose water surrounds the ear.
		/// @remark FMOD spreads a stereo source to full width at 180 degrees; beyond that its
		///	channels cross over and at 360 degrees they are swapped, so 180 is the widest bed.
		constexpr float MaxSpread = 180.0f;

		/// @brief Focus (length of the blended water direction) at or below which a bed voice
		///	is spread to MaxSpread.
		constexpr float WideFocus = 0.3f;

		/// @brief Focus at or above which a bed voice is narrowed to MinSpread. A riverbank with
		///	water on one side only scans at a focus of about 0.7, so it sits close to MinSpread.
		constexpr float NarrowFocus = 0.8f;

		/// @brief Largest scan range in world units a profile may use; longer ranges are clamped.
		constexpr float MaxRange = 100.0f;

		/// @brief Channel priority of the bed voices and the swim layer. Above the 1.0 of ordinary
		///	sounds: FMODAudio::FindAvailableChannel never steals a channel whose priority is higher
		///	than the requested sound's, so one-shots cannot take over these long-lived loops.
		constexpr float VoicePriority = 2.0f;

		/// @brief Maps a focus (length of the blended water direction, 0 = water all around the
		///	ear, 1 = water in exactly one direction) to the 3D spread of a bed voice.
		/// @param focus Focus in [0, 1]; values outside are clamped.
		/// @return Spread in degrees within [MinSpread, MaxSpread].
		[[nodiscard]] float SpreadFromFocus(float focus);
	}

	/// @brief Plays the water ambience around the listener.
	///
	///	Scans the water around the ear every water_ambience::ScanInterval, low-passes the scan
	///	result and drives per profile a calm and a fast bed loop (equal-power crossfaded by flow
	///	speed), placed in the direction of the water and widened by 3D spread when the water
	///	surrounds the ear. While swimming, a 2D swim layer of the swum water fades in; while the head
	///	is under water with an underwater sound, that 2D layer fades in and the swim layer out.
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
		///	Ranges are clamped to water_ambience::MaxRange. Profiles without a calm sound play no
		///	bed (and are not scanned); they may still provide a swim layer.
		/// @param profiles New profiles. An empty list disables the ambience.
		void SetProfiles(std::vector<WaterAmbienceSounds> profiles);

		/// @brief Advances scanning, smoothing and playback by one frame.
		/// @param source Source answering water samples for the scan.
		/// @param ear World position of the listener.
		/// @param swimming Whether the player currently swims.
		/// @param swimWaterType Water type the player swims in. Only used while swimming.
		/// @param submerged Whether the listener's head is under water.
		/// @param submergedWaterType Water type at the head. Only used while submerged; its profile
		///	selects the underwater sound.
		/// @param deltaSeconds Seconds since the last update.
		void Update(const IWaterAmbienceSource& source, const Vector3& ear, bool swimming, uint32 swimWaterType, bool submerged, uint32 submergedWaterType, float deltaSeconds);

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

		/// @brief Gets the live voices: the bed voices, then the swim layer and the underwater layer if they play.
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

		/// @brief A 2D loop layer which fades in and out as a whole.
		struct Layer
		{
			ChannelIndex channel = InvalidChannel;
			uint32 soundId = 0;
			float baseVolume = 1.0f;

			/// @brief Normalized fade in [0, 1].
			float fade = 0.0f;
		};

		/// @brief Starts, fades and stops the swim layer and the underwater layer.
		void UpdateSurfaceLayers(bool swimming, uint32 swimWaterType, bool submerged, uint32 submergedWaterType, float deltaSeconds);

		/// @brief Fades a layer towards a wanted sound; starts, switches and stops it as needed.
		/// @param layer Layer to drive.
		/// @param wantedSound Entry the layer should play, 0 for none.
		/// @param wantedVolume Profile volume of the wanted sound.
		/// @param fadeSeconds Full fade time.
		/// @param otherSound Entry the other layer plays; it is never started twice.
		/// @param deltaSeconds Seconds since the last update.
		void UpdateLayer(Layer& layer, uint32 wantedSound, float wantedVolume, float fadeSeconds, uint32 otherSound, float deltaSeconds);

		/// @brief Stops a layer immediately.
		void StopLayer(Layer& layer);

		/// @brief Applies volume, position and spread to a channel.
		void ApplyBedVoice(const BedVoice& voice) const;


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

		/// @brief Whether a smoothed field owns a direction: set when water is first heard, cleared
		///	once its level has faded to silence so new water starts from its own direction.
		std::vector<bool> m_hasDirection;

		/// @brief Seconds until the next scan.
		float m_scanCountdown = 0.0f;

		/// @brief Whether the next update must scan regardless of the countdown.
		bool m_scanDue = true;

		/// @brief Live bed voices by sound entry id.
		std::map<uint32, BedVoice> m_bedVoices;

		Layer m_swim;
		Layer m_underwater;

		/// @brief Sound entries which failed to start. They are not retried until the profiles
		///	change, so a broken entry does not log a warning every frame.
		std::set<uint32> m_failedEntries;

		Vector3 m_lastEar;
	};
}
