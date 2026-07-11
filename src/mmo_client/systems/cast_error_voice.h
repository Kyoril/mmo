// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "client_data/project.h"

#include <string>

namespace mmo
{
	class SoundEntryPlayer;

	/// @brief Plays race/gender specific voice lines when a spell cast of the active player
	/// fails (e.g. "Out of range.", "Not enough mana.").
	///
	/// The voice lines are configured as SoundEntry references in the race data
	/// (RaceEntry::male_voice / female_voice, keyed by spell cast result). Cast failures are
	/// reported by localization key from both the server failure packet handler and the
	/// client-side cast pre-validation, so this service maps keys back to cast result values.
	///
	/// Playback is throttled: while a voice line is (likely) still playing, further cast
	/// errors stay silent so spam-clicking an action button doesn't stack voices.
	class CastErrorVoice final : public NonCopyable
	{
	public:
		/// @brief Gets the singleton instance.
		static CastErrorVoice& Get();

	public:
		/// @brief Initializes the service with its dependencies. Passing nullptrs disables it.
		void Initialize(SoundEntryPlayer* player, const proto_client::RaceManager* races);

		/// @brief Sets the active character whose race/gender voice lines should be used.
		void SetCharacter(uint32 raceId, uint8 gender);

		/// @brief Notifies the service about a cast error, playing a voice line if one is
		/// configured for the active character and the throttle window has passed.
		/// @param errorKey The cast error localization key (e.g. "SPELL_CAST_FAILED_OUT_OF_RANGE").
		void OnCastError(const std::string& errorKey);

	private:
		CastErrorVoice() = default;

		/// @brief Resolves the SoundEntry id configured for the given cast result value.
		[[nodiscard]] uint32 ResolveSoundId(uint32 castResult) const;

	private:
		SoundEntryPlayer* m_player = nullptr;
		const proto_client::RaceManager* m_races = nullptr;
		uint32 m_raceId = 0;
		uint8 m_gender = 0;
		GameTime m_blockedUntil = 0;
	};
}
