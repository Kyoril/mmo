// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Substitutes the player name token in an npc chat line. Trigger driven Say/Yell/Emote
	/// lines are authored with the "$N" token - the same token quest and gossip texts use - which
	/// stands for the player the line is addressed to, meaning the unit that raised the trigger.
	/// Lines without an addressee are returned unchanged, so an unaddressed trigger never
	/// invents a name.
	/// @param text The already localized chat line.
	/// @param addresseeName Name of the player the line is addressed to, or an empty string.
	/// @returns The chat line with every "$N" / "$n" replaced by @p addresseeName.
	[[nodiscard]] String SubstituteChatTextTokens(const String& text, const String& addresseeName);
}
