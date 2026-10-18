// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "client_data/project.h"

namespace mmo
{
	/// @brief What the bot's Lua rotation needs to know about a spell.
	struct BotSpellInfo final
	{
		/// Damages an enemy and is neither passive nor meant for friends (IsOffensiveSpell).
		bool offensive { false };

		/// Maximum range in yards; 0 means the spell has no range limit of its own.
		float maxRange { 0.0f };

		uint32 castTime { 0 };
		uint32 cost { 0 };
		int32 powerType { 0 };

		/// Rough damage per second of cast time, for ordering spells only.
		float score { 0.0f };
	};

	/// @brief Reduces a client spell entry to the numbers the bot's rotation decides with, using
	///        the same rules as the headless bot (game/spell_offense.h).
	[[nodiscard]] BotSpellInfo DescribeBotSpell(const proto_client::SpellEntry& spell, const proto_client::RangeManager& ranges);
}
