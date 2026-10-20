// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "ambient_sound_emitters.h"

#include "sound_entry_player.h"

#include "log/default_log_levels.h"

#include <algorithm>

namespace mmo
{
	std::vector<uint64> SelectAmbientVoices(const std::span<const AmbientVoiceCandidate> candidates, const size_t maxVoices, const size_t maxPerEntry)
	{
		std::vector<const AmbientVoiceCandidate*> sorted;
		sorted.reserve(candidates.size());
		for (const AmbientVoiceCandidate& candidate : candidates)
		{
			sorted.push_back(&candidate);
		}

		std::stable_sort(sorted.begin(), sorted.end(), [](const AmbientVoiceCandidate* a, const AmbientVoiceCandidate* b)
		{
			return a->distanceSq < b->distanceSq;
		});

		std::vector<uint64> selected;
		std::unordered_map<uint32, size_t> perEntry;
		for (const AmbientVoiceCandidate* candidate : sorted)
		{
			if (selected.size() >= maxVoices)
			{
				break;
			}

			size_t& entryCount = perEntry[candidate->soundEntryId];
			if (entryCount >= maxPerEntry)
			{
				continue;
			}

			++entryCount;
			selected.push_back(candidate->key);
		}

		return selected;
	}

	AmbientSoundEmitters::AmbientSoundEmitters(SoundEntryPlayer& player, IAudio& audio, const uint32 randomSeed)
		: m_player(player)
		, m_audio(audio)
		, m_random(randomSeed)
	{
	}

	AmbientSoundEmitters::~AmbientSoundEmitters()
	{
		Clear();
	}

	void AmbientSoundEmitters::Add(const ambient_sound_owner::Type kind, const uint64 ownerId, std::vector<AmbientSoundSource> sources)
	{
		Remove(kind, ownerId);
		if (sources.empty())
		{
			return;
		}

		std::vector<uint64>& ids = m_owners[OwnerKey{ kind, ownerId }];
		ids.reserve(sources.size());
		for (AmbientSoundSource& source : sources)
		{
			const uint64 id = m_nextSourceId++;
			m_sources.emplace(id, std::move(source));
			ids.push_back(id);
		}

		// Let the new sources be heard on the next update rather than up to a quarter second later
		m_reselectTimer = 0.0f;
	}

	void AmbientSoundEmitters::Remove(const ambient_sound_owner::Type kind, const uint64 ownerId)
	{
		const auto it = m_owners.find(OwnerKey{ kind, ownerId });
		if (it == m_owners.end())
		{
			return;
		}

		for (const uint64 id : it->second)
		{
			m_sources.erase(id);
		}
		m_owners.erase(it);

		// Voices of removed sources fade out on their own: Reselect no longer finds their source
		m_reselectTimer = 0.0f;
	}

	void AmbientSoundEmitters::Clear()
	{
		for (Voice& voice : m_voices)
		{
			m_audio.StopSound(&voice.channel);
		}

		m_voices.clear();
		m_sources.clear();
		m_owners.clear();
		m_reselectTimer = 0.0f;
	}

	size_t AmbientSoundEmitters::GetActiveVoiceCount() const
	{
		return static_cast<size_t>(std::count_if(m_voices.begin(), m_voices.end(), [](const Voice& voice) { return !voice.retiring; }));
	}

	void AmbientSoundEmitters::Update(const Vector3& listener, const float deltaSeconds)
	{
		m_reselectTimer -= deltaSeconds;
		if (m_reselectTimer <= 0.0f)
		{
			Reselect(listener);
			m_reselectTimer = ReselectInterval;
		}

		for (auto it = m_voices.begin(); it != m_voices.end();)
		{
			Voice& voice = *it;
			if (voice.retiring)
			{
				voice.fade = voice.fadeOutPerSec > 0.0f ? voice.fade - voice.fadeOutPerSec * deltaSeconds : 0.0f;
				if (voice.fade <= 0.0f)
				{
					m_audio.StopSound(&voice.channel);
					it = m_voices.erase(it);
					continue;
				}
			}
			else if (voice.fade < 1.0f)
			{
				voice.fade = voice.fadeInPerSec > 0.0f ? std::min(1.0f, voice.fade + voice.fadeInPerSec * deltaSeconds) : 1.0f;
			}

			ApplyVolume(voice);
			++it;
		}
	}

	void AmbientSoundEmitters::Reselect(const Vector3& listener)
	{
		std::unordered_set<uint64> playing;
		for (const Voice& voice : m_voices)
		{
			if (!voice.retiring)
			{
				playing.insert(voice.sourceId);
			}
		}

		std::vector<AmbientVoiceCandidate> candidates;
		for (const auto& [id, source] : m_sources)
		{
			float range = source.maxDistance > 0.0f ? source.maxDistance : m_player.GetEntryMaxDistance(source.soundEntryId);
			if (range <= 0.0f)
			{
				continue;
			}

			if (playing.contains(id))
			{
				range *= KeepRangeFactor;
			}

			const float distanceSq = listener.GetSquaredDistanceTo(source.position);
			if (distanceSq <= range * range)
			{
				candidates.push_back(AmbientVoiceCandidate{ id, source.soundEntryId, distanceSq });
			}
		}

		// The source map is unordered: sort by id so that equal distances resolve the same way
		// on every selection, instead of swapping voices back and forth.
		std::sort(candidates.begin(), candidates.end(), [](const AmbientVoiceCandidate& a, const AmbientVoiceCandidate& b) { return a.key < b.key; });

		const std::vector<uint64> selectedIds = SelectAmbientVoices(candidates, MaxVoices, MaxVoicesPerEntry);
		const std::unordered_set<uint64> selected(selectedIds.begin(), selectedIds.end());

		for (Voice& voice : m_voices)
		{
			if (!voice.retiring && !selected.contains(voice.sourceId))
			{
				voice.retiring = true;
			}
		}

		for (const uint64 id : selectedIds)
		{
			if (!playing.contains(id))
			{
				StartVoice(id, m_sources.at(id));
			}
		}
	}

	void AmbientSoundEmitters::StartVoice(const uint64 sourceId, const AmbientSoundSource& source)
	{
		// The source left and came back while its voice was still fading out: fade that one back in
		for (Voice& voice : m_voices)
		{
			if (voice.sourceId == sourceId && voice.retiring)
			{
				voice.retiring = false;
				return;
			}
		}

		if (!IsPlayableEntry(source.soundEntryId))
		{
			return;
		}

		Voice voice;
		voice.sourceId = sourceId;
		voice.soundEntryId = source.soundEntryId;
		voice.channel = m_player.PlayEntry(source.soundEntryId, source.position);
		if (voice.channel == InvalidChannel)
		{
			return;
		}

		voice.baseVolume = m_player.GetEntryVolume(source.soundEntryId) * source.volume;
		voice.fadeInPerSec = 1.0f / std::max(MinFadeSeconds, m_player.GetEntryFadeInSeconds(source.soundEntryId));
		voice.fadeOutPerSec = 1.0f / std::max(MinFadeSeconds, m_player.GetEntryFadeOutSeconds(source.soundEntryId));

		if (const float length = m_player.GetEntryLength(source.soundEntryId); length > 0.0f)
		{
			if (IChannelInstance* channel = m_audio.GetChannelInstance(voice.channel))
			{
				channel->SetPlaybackPosition(std::uniform_real_distribution<float>(0.0f, length)(m_random));
			}
		}

		ApplyVolume(voice);
		m_voices.push_back(voice);
	}

	bool AmbientSoundEmitters::IsPlayableEntry(const uint32 soundEntryId)
	{
		if (m_player.IsEntry3D(soundEntryId) && m_player.IsEntryLooped(soundEntryId))
		{
			return true;
		}

		if (m_rejectedEntries.insert(soundEntryId).second)
		{
			WLOG("Ambient sound entry " << soundEntryId << " is not a looped 3D sound; mesh sounds referencing it stay silent");
		}

		return false;
	}

	void AmbientSoundEmitters::ApplyVolume(const Voice& voice) const
	{
		if (IChannelInstance* channel = m_audio.GetChannelInstance(voice.channel))
		{
			channel->SetVolume(voice.baseVolume * voice.fade);
		}
	}
}
