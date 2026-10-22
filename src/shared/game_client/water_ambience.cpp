// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_ambience.h"

#include "sound_entry_player.h"

#include "math/constants.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mmo
{
	namespace
	{
		/// Volume at or below which a voice counts as silent.
		constexpr float SilentVolume = 0.001f;
	}

	WaterAmbience::WaterAmbience(SoundEntryPlayer& player, IAudio& audio, const uint32 randomSeed)
		: m_player(player)
		, m_audio(audio)
		, m_random(randomSeed)
	{
	}

	WaterAmbience::~WaterAmbience()
	{
		Stop();
	}

	void WaterAmbience::SetProfiles(std::vector<WaterAmbienceSounds> profiles)
	{
		Stop();

		m_profiles = std::move(profiles);
		m_scanProfiles.clear();
		m_scanProfiles.reserve(m_profiles.size());
		for (const WaterAmbienceSounds& sounds : m_profiles)
		{
			m_scanProfiles.push_back(sounds.profile);
		}

		m_targetFields.assign(m_profiles.size(), WaterAmbienceField());
		m_smoothedFields.assign(m_profiles.size(), WaterAmbienceField());
		m_hasDirection.assign(m_profiles.size(), false);
		m_failedEntries.clear();
		for (size_t i = 0; i < m_profiles.size(); ++i)
		{
			m_targetFields[i].waterType = m_profiles[i].profile.waterType;
			m_smoothedFields[i].waterType = m_profiles[i].profile.waterType;
		}
	}

	void WaterAmbience::Update(const IWaterAmbienceSource& source, const Vector3& ear, const bool swimming, const uint32 swimWaterType, const float deltaSeconds)
	{
		m_lastEar = ear;

		if (m_profiles.empty())
		{
			return;
		}

		m_scanCountdown -= deltaSeconds;
		if (m_scanDue || m_scanCountdown <= 0.0f)
		{
			m_targetFields = ScanWaterAmbience(source, ear, m_scanProfiles);
			m_scanDue = false;
			m_scanCountdown = std::max(m_scanCountdown + water_ambience::ScanInterval, 0.0f);
		}

		Smooth(deltaSeconds);
		UpdateBedVoices(ear, deltaSeconds);
		UpdateSwimLayer(swimming, swimWaterType, deltaSeconds);
	}

	void WaterAmbience::Stop()
	{
		for (auto& [soundId, voice] : m_bedVoices)
		{
			m_audio.StopSound(&voice.channel);
		}
		m_bedVoices.clear();

		StopSwimLayer();

		for (size_t i = 0; i < m_smoothedFields.size(); ++i)
		{
			const uint32 waterType = m_smoothedFields[i].waterType;
			m_smoothedFields[i] = WaterAmbienceField();
			m_smoothedFields[i].waterType = waterType;
			m_hasDirection[i] = false;
		}

		m_scanDue = true;
		m_scanCountdown = 0.0f;
	}

	const std::vector<WaterAmbienceField>& WaterAmbience::GetSmoothedFields() const
	{
		return m_smoothedFields;
	}

	std::vector<WaterAmbience::VoiceInfo> WaterAmbience::GetVoices() const
	{
		std::vector<VoiceInfo> voices;
		voices.reserve(m_bedVoices.size() + 1);

		for (const auto& [soundId, voice] : m_bedVoices)
		{
			voices.push_back({ soundId, voice.volume, voice.spread, voice.position });
		}

		if (m_swimChannel != InvalidChannel)
		{
			voices.push_back({ m_swimSoundId, m_swimBaseVolume * m_swimFade, water_ambience::MaxSpread, m_lastEar });
		}

		return voices;
	}

	void WaterAmbience::Smooth(const float deltaSeconds)
	{
		const float alpha = 1.0f - std::exp(-std::max(deltaSeconds, 0.0f) / water_ambience::SmoothingSeconds);

		for (size_t i = 0; i < m_smoothedFields.size(); ++i)
		{
			const WaterAmbienceField& target = m_targetFields[i];
			WaterAmbienceField& smoothed = m_smoothedFields[i];

			smoothed.level += (target.level - smoothed.level) * alpha;

			// Without water the scan has neither a direction nor a speed: keep the last ones so
			// a fading voice neither swings onto the ear nor changes its loop mix.
			if (target.level <= 0.0f)
			{
				continue;
			}

			// Water heard for the first time has no previous direction to blend from.
			if (!m_hasDirection[i])
			{
				smoothed.direction = target.direction;
				smoothed.speed = target.speed;
				m_hasDirection[i] = true;
				continue;
			}

			smoothed.direction += (target.direction - smoothed.direction) * alpha;
			smoothed.speed += (target.speed - smoothed.speed) * alpha;
		}
	}

	std::map<uint32, WaterAmbience::BedTarget> WaterAmbience::GatherBedTargets(const Vector3& ear) const
	{
		// Two profiles may share a sound entry, but a streamed entry plays on one channel only.
		// Picking the louder profile would make the voice jump sides whenever the winner flips,
		// so the candidates are blended: volume-weighted direction, equal-power volume.
		struct Blend
		{
			float volumeSum = 0.0f;
			float volumeSquareSum = 0.0f;
			Vector3 weightedDirection;

			// Unweighted sum, used while every candidate is silent.
			Vector3 directionSum;
			int count = 0;

			float range = 0.0f;
		};

		std::map<uint32, Blend> blends;

		const auto offer = [this, &blends](const uint32 soundId, const float volume, const Vector3& direction, const float range)
		{
			if (soundId == 0)
			{
				return;
			}

			const float entryVolume = volume * m_player.GetEntryVolume(soundId);

			Blend& blend = blends[soundId];
			blend.volumeSum += entryVolume;
			blend.volumeSquareSum += entryVolume * entryVolume;
			blend.weightedDirection += direction * entryVolume;
			blend.directionSum += direction;
			++blend.count;
			blend.range = std::max(blend.range, range);
		};

		for (size_t i = 0; i < m_profiles.size(); ++i)
		{
			const WaterAmbienceSounds& sounds = m_profiles[i];
			const WaterAmbienceField& field = m_smoothedFields[i];

			const float speed = std::clamp(field.speed, 0.0f, 1.0f);
			const float calmWeight = sounds.fastSound == 0 ? 1.0f : std::cos(speed * HalfPi);
			const float fastWeight = std::sin(speed * HalfPi);
			const float baseVolume = sounds.volume * field.level;

			offer(sounds.calmSound, baseVolume * calmWeight, field.direction, sounds.profile.range);
			offer(sounds.fastSound, baseVolume * fastWeight, field.direction, sounds.profile.range);
		}

		std::map<uint32, BedTarget> targets;
		for (const auto& [soundId, blend] : blends)
		{
			BedTarget& target = targets[soundId];
			target.range = blend.range;
			target.volume = std::sqrt(blend.volumeSquareSum);

			// Weighted mean: a single candidate keeps its own direction exactly. Opposite
			// candidates cancel out, which pulls the voice towards the ear and widens it.
			const Vector3 direction = blend.volumeSum > 0.0f
				? blend.weightedDirection / blend.volumeSum
				: blend.directionSum / static_cast<float>(blend.count);

			// The direction is never normalised: its length is the focus, so the voice glides
			// through the ear as the water spreads around the listener instead of snapping.
			const float focus = std::clamp(direction.GetLength(), 0.0f, 1.0f);
			target.spread = water_ambience::MinSpread + (1.0f - focus) * (water_ambience::MaxSpread - water_ambience::MinSpread);
			target.position = ear + direction * water_ambience::VirtualDistance;
		}

		return targets;
	}

	void WaterAmbience::UpdateBedVoices(const Vector3& ear, const float deltaSeconds)
	{
		const std::map<uint32, BedTarget> targets = GatherBedTargets(ear);

		// Start voices which became audible.
		for (const auto& [soundId, target] : targets)
		{
			if (target.volume <= SilentVolume || m_bedVoices.count(soundId) != 0 || m_failedEntries.count(soundId) != 0)
			{
				continue;
			}

			BedVoice voice;
			voice.channel = m_player.PlayEntryPositionedLoop(soundId, target.position, water_ambience::VirtualDistance, target.range);
			if (voice.channel == InvalidChannel)
			{
				m_failedEntries.insert(soundId);
				continue;
			}

			// Start somewhere within the loop so that every visit does not open identically.
			if (const float length = m_player.GetEntryLength(soundId); length > 0.0f)
			{
				if (IChannelInstance* channel = m_audio.GetChannelInstance(voice.channel))
				{
					channel->SetPlaybackPosition(std::uniform_real_distribution<float>(0.0f, length)(m_random));
				}
			}

			m_bedVoices.emplace(soundId, voice);
		}

		// Adjust live voices; stop the ones which stayed silent long enough.
		for (auto it = m_bedVoices.begin(); it != m_bedVoices.end();)
		{
			BedVoice& voice = it->second;

			const auto targetIt = targets.find(it->first);
			if (targetIt != targets.end())
			{
				voice.volume = targetIt->second.volume;
				voice.spread = targetIt->second.spread;
				voice.position = targetIt->second.position;
			}
			else
			{
				voice.volume = 0.0f;
			}

			if (voice.volume <= SilentVolume)
			{
				voice.silentSeconds += deltaSeconds;
				if (voice.silentSeconds >= water_ambience::StopHoldSeconds)
				{
					m_audio.StopSound(&voice.channel);
					it = m_bedVoices.erase(it);
					continue;
				}
			}
			else
			{
				voice.silentSeconds = 0.0f;
			}

			ApplyBedVoice(voice);
			++it;
		}
	}

	void WaterAmbience::UpdateSwimLayer(const bool swimming, const uint32 swimWaterType, const float deltaSeconds)
	{
		uint32 wantedSound = 0;
		float wantedVolume = 1.0f;
		if (swimming)
		{
			for (const WaterAmbienceSounds& sounds : m_profiles)
			{
				if (sounds.profile.waterType == swimWaterType && sounds.swimSound != 0)
				{
					wantedSound = sounds.swimSound;
					wantedVolume = sounds.volume;
					break;
				}
			}
		}

		if (m_swimChannel == InvalidChannel)
		{
			if (wantedSound == 0 || m_failedEntries.count(wantedSound) != 0)
			{
				return;
			}

			m_swimChannel = m_player.PlayEntry(wantedSound);
			if (m_swimChannel == InvalidChannel)
			{
				m_failedEntries.insert(wantedSound);
				return;
			}

			m_swimSoundId = wantedSound;
			m_swimBaseVolume = m_player.GetEntryVolume(wantedSound) * wantedVolume;
			m_swimFade = 0.0f;
		}

		const float step = std::max(deltaSeconds, 0.0f) / water_ambience::SwimFadeSeconds;
		if (m_swimSoundId == wantedSound)
		{
			m_swimFade = std::min(1.0f, m_swimFade + step);
		}
		else
		{
			// Leaving the water, or swimming into another water's layer: fade out first.
			m_swimFade = std::max(0.0f, m_swimFade - step);
			if (m_swimFade <= 0.0f)
			{
				StopSwimLayer();
				return;
			}
		}

		if (IChannelInstance* channel = m_audio.GetChannelInstance(m_swimChannel))
		{
			channel->SetVolume(m_swimBaseVolume * m_swimFade);
		}
	}

	void WaterAmbience::ApplyBedVoice(const BedVoice& voice) const
	{
		m_audio.Set3DPosition(voice.channel, voice.position);

		if (IChannelInstance* channel = m_audio.GetChannelInstance(voice.channel))
		{
			channel->SetVolume(voice.volume);
			channel->Set3DSpread(voice.spread);
		}
	}

	void WaterAmbience::StopSwimLayer()
	{
		if (m_swimChannel != InvalidChannel)
		{
			m_audio.StopSound(&m_swimChannel);
		}

		m_swimChannel = InvalidChannel;
		m_swimSoundId = 0;
		m_swimFade = 0.0f;
	}
}
