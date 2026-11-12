// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	/// Returns the order index to select initially, or -1 for an empty roster.
	/// Falls back to 0 if the last played character no longer exists.
	/// @param orderedGuids Character guids in roster order.
	/// @param lastPlayedGuid Guid of the realm's last played character, or 0.
	int32 ResolveInitialSelection(const std::vector<uint64>& orderedGuids, uint64 lastPlayedGuid);

	/// Moves the entry at from so that it ends up at index to. Out of range indices change nothing.
	/// @param order Character guids in roster order.
	/// @param from Current index of the entry.
	/// @param to Index the entry ends up at.
	void MoveRosterEntry(std::vector<uint64>& order, size_t from, size_t to);

	/// Reads the last played character of a realm from a "realmId:guid;realmId:guid" value.
	/// @param cvarValue The value of the lastCharacters console variable.
	/// @param realmId The realm.
	/// @return The guid, or 0 if not found or malformed.
	uint64 GetLastCharacterForRealm(const String& cvarValue, uint32 realmId);

	/// Returns the value with the realm's last played character replaced or added.
	/// @param cvarValue The value of the lastCharacters console variable.
	/// @param realmId The realm.
	/// @param guid The character's guid.
	String SetLastCharacterForRealm(const String& cvarValue, uint32 realmId, uint64 guid);
}
