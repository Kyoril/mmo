// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Replaces the authored name token ($N / $n) in a chat line that an npc speaks. Authored
	/// npc text addresses the player through the same tokens as quest and gossip text (see the
	/// quest writing guide in docs/world/bible.md), but chat lines are composed on the server
	/// and reach the client as plain text, so they have to be substituted here.
	/// @param text The authored text, already resolved for the recipients' locale.
	/// @param name The name to insert for the token. An empty name drops the token, so that a
	///             raw "$N" never reaches a player.
	/// @returns The text with every name token replaced. Any other '$' sequence is kept as is.
	String SubstituteChatNameToken(const String& text, const String& name);
}
