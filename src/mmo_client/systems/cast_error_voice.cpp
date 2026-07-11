// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cast_error_voice.h"

#include "base/clock.h"
#include "game/gender.h"
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
			// All power type refinements ("..._NO_POWER_MANA" etc.) share one voice line.
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

			return spell_cast_result::CastOkay;
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

		const uint32 soundId = ResolveSoundId(castResult);
		if (soundId == 0)
		{
			return;
		}

		// Don't stack voice lines when errors are spammed: block until the previous line
		// has most likely finished playing. Time based on purpose - channels are recycled,
		// so a stored channel index could no longer belong to the voice line.
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

	uint32 CastErrorVoice::ResolveSoundId(const uint32 castResult) const
	{
		const proto_client::RaceEntry* race = m_races->getById(m_raceId);
		if (!race)
		{
			return 0;
		}

		const proto_client::VoiceLineSet* voiceSet = nullptr;
		if (m_gender == Female && race->has_female_voice())
		{
			voiceSet = &race->female_voice();
		}
		else if (m_gender == Male && race->has_male_voice())
		{
			voiceSet = &race->male_voice();
		}

		if (!voiceSet)
		{
			return 0;
		}

		const auto it = voiceSet->cast_error_sounds().find(castResult);
		if (it == voiceSet->cast_error_sounds().end())
		{
			return 0;
		}

		return it->second;
	}
}
