// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <unordered_map>

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Keeps a unit from talking over itself in combat. One gate per unit covers both
	///	the attacker's effort voice and the victim's pain react, so the two never overlap.
	class CombatVoiceGate final
	{
	public:
		/// @brief Asks whether the given unit may play a voice line right now, arming the
		///	gate when it may.
		/// @param unit Guid of the speaking unit.
		/// @param now Current time in milliseconds.
		/// @param blockDuration How long the unit stays silent afterwards, in milliseconds.
		/// @return true when the line may play, false while the unit is still blocked.
		bool TryPlay(ObjectGuid unit, GameTime now, GameTime blockDuration);

		/// @brief Drops all per-unit state, for example on a world change.
		void Clear();

	private:
		/// @brief Drops entries whose block window has passed, so the map cannot grow
		///	without bound over a long session.
		void Prune(GameTime now);

	private:
		/// Earliest time each unit may speak again.
		std::unordered_map<ObjectGuid, GameTime> m_nextAllowed;
	};
}
