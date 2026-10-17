// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <unordered_map>

#include "base/typedefs.h"

namespace mmo
{
	/// How long the same auto attack error voice line stays silent after it was spoken.
	constexpr GameTime attackErrorVoiceRepeatCooldownMs = 8000;

	/// @brief Keeps an auto attack error voice line ("Out of range.", "I have to be facing
	///	the target.") from being repeated over and over.
	///
	///	Auto attack re-reports its error on every swing attempt for as long as the condition
	///	holds, so without a gate the same line is spoken every couple of seconds. Each line
	///	gets its own cooldown: a line which is *different* from the one just spoken plays at
	///	once, because it tells the player something new.
	///
	///	Only auto attack errors go through this gate. Regular spell cast errors are the
	///	direct result of a player action and keep answering every single attempt.
	class AttackErrorVoiceGate final
	{
	public:
		/// @brief Asks whether the given voice line may play right now.
		/// @param soundId SoundEntry id of the voice line.
		/// @param now Current time in milliseconds.
		/// @return true when the line may play, false while its cooldown is still running.
		[[nodiscard]] bool CanPlay(uint32 soundId, GameTime now) const;

		/// @brief Records that the line was played, blocking a repeat of that same line.
		/// @param soundId SoundEntry id of the voice line which played.
		/// @param now Current time in milliseconds.
		/// @param repeatCooldown How long the same line stays silent, in milliseconds.
		void NotifyPlayed(uint32 soundId, GameTime now, GameTime repeatCooldown);

		/// @brief Drops all state, for example when the active character changes.
		void Clear();

	private:
		/// Earliest time each voice line may be spoken again. Bounded by the number of
		/// attack swing events which have a voice line configured, so it needs no pruning.
		std::unordered_map<uint32, GameTime> m_nextAllowed;
	};
}
