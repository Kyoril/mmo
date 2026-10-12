// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_sound_player.h"

#include <random>

#include "base/clock.h"
#include "sound_entry_player.h"

namespace mmo
{
	namespace
	{
		/// Extra silence appended after a voice clip so two lines never butt up against
		/// each other, matching what the cast error voice does.
		constexpr GameTime voiceTailMs = 250;

		/// How long a unit has to stay out of combat before entering it again counts as a new
		/// pull. The server drops the combat flag whenever the last attacker stops swinging,
		/// even mid-fight, and an aggro roar on every such flicker would wear out fast.
		constexpr GameTime aggroRearmMs = 5000;

		/// How long a dead unit stays silent beyond its death sound. Covers the pain react
		/// of the killing blow, which waits for the weapon to visually connect.
		constexpr GameTime deathSilenceMs = 3000;

		/// Above this many tracked units, units that left combat long ago are forgotten.
		constexpr size_t combatPruneThreshold = 256;

		uint32 rollPercent()
		{
			static std::random_device rd;
			static std::mt19937 gen(rd());
			std::uniform_int_distribution<uint32> dis(0, 99);
			return dis(gen);
		}
	}

	CombatSoundPlayer::CombatSoundPlayer(const proto_client::Project& project, SoundEntryPlayer& soundPlayer)
		: m_project(project)
		, m_soundPlayer(soundPlayer)
		, m_roll(rollPercent)
		, m_clock(GetAsyncTimeMs)
	{
	}

	void CombatSoundPlayer::PlaySwing(const CombatSoundAttacker& attacker, const ObjectGuid attackerGuid,
		const Vector3& attackerPos, const bool crit)
	{
		Play(combat_sounds::ResolveSwingSound(m_project.itemSubclasses, m_project.models, attacker), attackerPos);

		TryPlayVoice(combat_sounds::ResolveAttackVoice(m_project.models, attacker.displayId, crit),
			attackerGuid, attacker.displayId, attackerPos);
	}

	void CombatSoundPlayer::PlayOutcome(const CombatSoundAttacker& attacker, const CombatSoundVictim& victim,
		const CombatSwingOutcome& outcome, const ObjectGuid victimGuid, const Vector3& victimPos)
	{
		const ResolvedSwingSounds resolved = combat_sounds::ResolveSwingSounds(
			m_project.itemSubclasses, m_project.models, attacker, victim, outcome);

		Play(resolved.impactSound, victimPos);
		Play(resolved.critLayerSound, victimPos);
		Play(resolved.missSound, victimPos);
		Play(resolved.parrySound, victimPos);
		Play(resolved.blockSound, victimPos);

		if (outcome.landed)
		{
			TryPlayVoice(combat_sounds::ResolveHitVoice(m_project.models, victim.displayId, outcome.crit),
				victimGuid, victim.displayId, victimPos);
		}
	}

	void CombatSoundPlayer::NotifyCombatState(const ObjectGuid unit, const uint32 displayId, const bool inCombat,
		const Vector3& position)
	{
		const GameTime now = m_clock();

		if (!inCombat)
		{
			if (m_inCombat.erase(unit) > 0)
			{
				if (m_leftCombatAt.size() >= combatPruneThreshold)
				{
					std::erase_if(m_leftCombatAt, [now](const auto& entry) { return now - entry.second >= aggroRearmMs; });
				}

				m_leftCombatAt[unit] = now;
			}
			return;
		}

		if (!m_inCombat.insert(unit).second)
		{
			return;
		}

		if (const auto it = m_leftCombatAt.find(unit); it != m_leftCombatAt.end())
		{
			const bool rearmed = now - it->second >= aggroRearmMs;
			m_leftCombatAt.erase(it);
			if (!rearmed)
			{
				return;
			}
		}

		PlayForcedVoice(combat_sounds::ResolveAggroSound(m_project.models, displayId), unit, position, voiceTailMs);
	}

	void CombatSoundPlayer::PlayDeath(const ObjectGuid unit, const uint32 displayId, const Vector3& position)
	{
		m_inCombat.erase(unit);
		m_leftCombatAt.erase(unit);

		PlayForcedVoice(combat_sounds::ResolveDeathSound(m_project.models, displayId), unit, position, deathSilenceMs);
	}

	void CombatSoundPlayer::Clear()
	{
		m_voiceGate.Clear();
		m_inCombat.clear();
		m_leftCombatAt.clear();
	}

	void CombatSoundPlayer::SetRollProvider(std::function<uint32()> roll)
	{
		m_roll = std::move(roll);
	}

	void CombatSoundPlayer::SetClock(std::function<GameTime()> clock)
	{
		m_clock = std::move(clock);
	}

	void CombatSoundPlayer::TryPlayVoice(const CombatVoiceRequest& request, const ObjectGuid unit,
		const uint32 displayId, const Vector3& position)
	{
		if (request.sound == 0 || request.chance == 0)
		{
			return;
		}

		if (request.chance < 100 && m_roll() >= request.chance)
		{
			return;
		}

		if (!m_voiceGate.TryPlay(unit, m_clock(), GetVoiceBlockDuration(displayId, request.sound)))
		{
			return;
		}

		Play(request.sound, position);
	}

	GameTime CombatSoundPlayer::GetVoiceBlockDuration(const uint32 displayId, const uint32 sound) const
	{
		if (const auto* model = m_project.models.getById(displayId))
		{
			if (const uint32 authored = model->combat_sounds().voice_min_interval_ms())
			{
				return authored;
			}
		}

		const float length = m_soundPlayer.GetEntryLength(sound);
		return static_cast<GameTime>(length * 1000.0f) + voiceTailMs;
	}

	void CombatSoundPlayer::PlayForcedVoice(const uint32 sound, const ObjectGuid unit, const Vector3& position,
		const GameTime extraSilence)
	{
		if (sound == 0)
		{
			return;
		}

		const float length = m_soundPlayer.GetEntryLength(sound);
		m_voiceGate.Silence(unit, m_clock(), static_cast<GameTime>(length * 1000.0f) + extraSilence);

		Play(sound, position);
	}

	void CombatSoundPlayer::Play(const uint32 sound, const Vector3& position) const
	{
		if (sound == 0)
		{
			return;
		}

		m_soundPlayer.PlayEntry(sound, position);
	}
}
