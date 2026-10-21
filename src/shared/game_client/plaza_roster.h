// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <array>
#include <vector>

namespace mmo
{
	/// Number of fixed standing spots on the plaza.
	constexpr int32 PlazaSpotCount = 5;

	/// Which character stands where on the plaza.
	struct PlazaAssignment
	{
		/// Order index of the character standing at spot i, or -1. The selected plaza
		/// character's own spot is -1, because they stand at the front.
		std::array<int32, PlazaSpotCount> spotCharacter;

		/// Order index of the character at the front, or -1.
		int32 frontCharacter;

		/// True if the front character is not one of the plaza characters (a visitor).
		bool frontIsVisitor;
	};

	/// Decides which character stands at which spot for the given selection.
	PlazaAssignment AssignPlaza(int32 numCharacters, int32 selectedIndex);

	/// Returns the order index to select initially, or -1 for an empty roster.
	/// Falls back to 0 if the last played character no longer exists.
	int32 ResolveInitialSelection(const std::vector<uint64>& orderedGuids, uint64 lastPlayedGuid);

	/// Moves the entry at from so that it ends up at index to.
	void MoveRosterEntry(std::vector<uint64>& order, size_t from, size_t to);

	/// Reads the last played character of a realm from a "realmId:guid;realmId:guid" value.
	/// Returns 0 if not found or malformed.
	uint64 GetLastCharacterForRealm(const String& cvarValue, uint32 realmId);

	/// Returns the value with the realm's last played character replaced or added.
	String SetLastCharacterForRealm(const String& cvarValue, uint32 realmId, uint64 guid);

	/// How the glue camera arrives at the plaza.
	enum class GlueArrival
	{
		FlyIn,
		FadeWhenReady,
		Snap
	};

	/// Decides how the glue scene should be presented.
	GlueArrival DecideGlueArrival(bool plazaReady, bool returningFromWorld);
}
