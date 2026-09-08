// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cast_error_voice.h"

#include "base/clock.h"
#include "game/gender.h"
#include "game/auto_attack.h"
#include "game/spell.h"
#include "game_client/sound_entry_player.h"

namespace mmo
{
	namespace
	{
		/// Maps a cast error localization key back to its spell_cast_result value.
		/// Only errors which support voice lines need to be listed here.
		uint32 GetCastResultFromErrorKey(const std::string& errorKey)
		{
			// All power type refinements ("..._NO_POWER_MANA" etc.) map to the same cast
			// result; the power type itself is resolved separately for the voice line.
			if (errorKey.rfind("SPELL_CAST_FAILED_NO_POWER", 0) == 0)
			{
				return spell_cast_result::FailedNoPower;
			}

			if (errorKey == "SPELL_CAST_FAILED_OUT_OF_RANGE")
			{
				return spell_cast_result::FailedOutOfRange;
			}

			if (errorKey == "SPELL_CAST_FAILED_NOT_READY")
			{
				return spell_cast_result::FailedNotReady;
			}

			if (errorKey == "SPELL_CAST_FAILED_BAD_TARGETS" ||
				errorKey == "SPELL_CAST_FAILED_TARGET_FRIENDLY")
			{
				// TARGET_FRIENDLY is a client-only refinement of the same mistake -- the player
				// aimed a hostile spell at something it can never work on.
				return spell_cast_result::FailedBadTargets;
			}

			if (errorKey == "SPELL_CAST_FAILED_CASTER_DEAD")
			{
				return spell_cast_result::FailedCasterDead;
			}

			if (errorKey == "SPELL_CAST_FAILED_UNIT_NOT_INFRONT")
			{
				return spell_cast_result::FailedUnitNotInfront;
			}

			return spell_cast_result::CastOkay;
		}

		/// Maps an attack swing error event name back to its attack_swing_event value.
		/// Returns attack_swing_event::Unknown for events which support no voice line.
		uint32 GetAttackEventFromErrorEvent(const std::string& errorEvent)
		{
			if (errorEvent == "ATTACK_SWING_CANT_ATTACK")
			{
				return attack_swing_event::CantAttack;
			}

			if (errorEvent == "ATTACK_SWING_TARGET_DEAD")
			{
				return attack_swing_event::TargetDead;
			}

			if (errorEvent == "ATTACK_SWING_WRONG_FACING")
			{
				return attack_swing_event::WrongFacing;
			}

			if (errorEvent == "ATTACK_SWING_OUT_OF_RANGE")
			{
				return attack_swing_event::OutOfRange;
			}

			if (errorEvent == "ATTACK_SWING_NOT_STANDING")
			{
				return attack_swing_event::NotStanding;
			}

			return attack_swing_event::Unknown;
		}

		/// Extracts the power type from a refined no-power error key
		/// ("SPELL_CAST_FAILED_NO_POWER_MANA" etc.), or power_type::Invalid_ when the
		/// key is not a no-power error or carries no power refinement.
		int32 GetPowerTypeFromErrorKey(const std::string& errorKey)
		{
			if (errorKey == "SPELL_CAST_FAILED_NO_POWER_MANA")
			{
				return power_type::Mana;
			}

			if (errorKey == "SPELL_CAST_FAILED_NO_POWER_RAGE")
			{
				return power_type::Rage;
			}

			if (errorKey == "SPELL_CAST_FAILED_NO_POWER_ENERGY")
			{
				return power_type::Energy;
			}

			if (errorKey == "SPELL_CAST_FAILED_NO_POWER_HEALTH")
			{
				return power_type::Health;
			}

			return power_type::Invalid_;
		}
	}

	CastErrorVoice& CastErrorVoice::Get()
	{
		static CastErrorVoice s_instance;
		return s_instance;
	}

	void CastErrorVoice::Initialize(SoundEntryPlayer* player, const proto_client::RaceManager* races)
	{
		m_player = player;
		m_races = races;
		m_blockedUntil = 0;
	}

	void CastErrorVoice::SetCharacter(const uint32 raceId, const uint8 gender)
	{
		m_raceId = raceId;
		m_gender = gender;
	}

	void CastErrorVoice::OnCastError(const std::string& errorKey)
	{
		if (!m_player || !m_races)
		{
			return;
		}

		const uint32 castResult = GetCastResultFromErrorKey(errorKey);
		if (castResult == spell_cast_result::CastOkay)
		{
			return;
		}

		PlayThrottled(ResolveSoundId(castResult, GetPowerTypeFromErrorKey(errorKey)));
	}

	void CastErrorVoice::OnAttackSwingError(const std::string& errorEvent)
	{
		if (!m_player || !m_races)
		{
			return;
		}

		const uint32 attackEvent = GetAttackEventFromErrorEvent(errorEvent);
		if (attackEvent == attack_swing_event::Unknown)
		{
			return;
		}

		PlayThrottled(ResolveAttackSoundId(attackEvent));
	}

	void CastErrorVoice::PlayThrottled(const uint32 soundId)
	{
		if (soundId == 0)
		{
			return;
		}

		// Don't stack voice lines when errors are spammed: block until the previous line
		// has most likely finished playing. Time based on purpose - channels are recycled,
		// so a stored channel index could no longer belong to the voice line.
		// Cast errors and attack swing errors share one window: an out-of-range auto attack
		// re-fires its error on a timer, and the two would otherwise talk over each other.
		const GameTime now = GetAsyncTimeMs();
		if (now < m_blockedUntil)
		{
			return;
		}

		if (m_player->PlayEntry(soundId) == InvalidChannel)
		{
			return;
		}

		constexpr GameTime extraGapMs = 250;
		m_blockedUntil = now + static_cast<GameTime>(m_player->GetEntryLength(soundId) * 1000.0f) + extraGapMs;
	}

	const proto_client::VoiceLineSet* CastErrorVoice::GetVoiceSet() const
	{
		const proto_client::RaceEntry* race = m_races->getById(m_raceId);
		if (!race)
		{
			return nullptr;
		}

		if (m_gender == Female && race->has_female_voice())
		{
			return &race->female_voice();
		}

		if (m_gender == Male && race->has_male_voice())
		{
			return &race->male_voice();
		}

		return nullptr;
	}

	uint32 CastErrorVoice::ResolveAttackSoundId(const uint32 attackEvent) const
	{
		const proto_client::VoiceLineSet* voiceSet = GetVoiceSet();
		if (!voiceSet)
		{
			return 0;
		}

		const auto it = voiceSet->attack_error_sounds().find(attackEvent);
		if (it == voiceSet->attack_error_sounds().end())
		{
			return 0;
		}

		return it->second;
	}

	uint32 CastErrorVoice::ResolveSoundId(const uint32 castResult, const int32 powerType) const
	{
		const proto_client::VoiceLineSet* voiceSet = GetVoiceSet();
		if (!voiceSet)
		{
			return 0;
		}

		// No-power errors prefer the voice line of the missing power type ("No mana!",
		// "Not enough rage!", ...) and fall back to the generic no-power line.
		if (castResult == spell_cast_result::FailedNoPower && powerType != power_type::Invalid_)
		{
			const auto powerIt = voiceSet->no_power_sounds().find(static_cast<uint32>(powerType));
			if (powerIt != voiceSet->no_power_sounds().end())
			{
				return powerIt->second;
			}
		}

		const auto it = voiceSet->cast_error_sounds().find(castResult);
		if (it == voiceSet->cast_error_sounds().end())
		{
			return 0;
		}

		return it->second;
	}
}
