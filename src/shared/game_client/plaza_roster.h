// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/character_view.h"
#include "game_common/plaza_spot_count.h"

#include <array>
#include <unordered_map>
#include <vector>

namespace mmo
{
	/// Everything of a character view that changes how its plaza model looks, so an avatar can be
	/// rebuilt when a character with the same guid looks different (a changed appearance, other
	/// gear, or another realm's character with a colliding guid).
	struct PlazaAppearance
	{
		uint32 displayId = 0;
		uint32 raceId = 0;
		uint8 gender = 0;
		std::unordered_map<uint32, uint32> chosenOptionPerGroup;
		std::unordered_map<uint32, float> scalarValues;
		std::array<uint32, player_equipment_slots::Count_> equipmentDisplayIds{};

		/// Whether both describe the same model.
		[[nodiscard]] bool operator==(const PlazaAppearance& other) const;

		/// Whether the models differ.
		[[nodiscard]] bool operator!=(const PlazaAppearance& other) const { return !(*this == other); }
	};

	/// Takes the appearance of a character view.
	[[nodiscard]] PlazaAppearance MakePlazaAppearance(const CharacterView& view);

	/// Which character stands where on the plaza.
	struct PlazaAssignment
	{
		/// Order index of the character standing at spot i, or -1. The plaza characters (order
		/// index below PlazaSpotCount) always stand at their own spot, selected or not.
		std::array<int32, PlazaSpotCount> spotCharacter;

		/// Order index of the selected visitor (a character beyond the spots) standing at the
		/// visitor spot, or -1.
		int32 visitor;
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
		/// Lift the fade on the gate view and fly to the plaza now.
		FlyIn,
		/// Keep the scene covered until gate and plaza are ready (or their readiness timeouts
		/// fire), then lift the fade on the gate view and fly to the plaza.
		FlyInWhenReady,
		/// Cut straight to the plaza now (back from the world).
		Snap,
		/// Keep the scene covered until the plaza is ready, then cut straight to it (back from
		/// the world).
		SnapWhenReady
	};

	/// Decides how the glue scene should be presented when the character list arrives.
	/// @param plazaReady Whether the plaza view is ready (it only becomes ready after the gate).
	/// @param returningFromWorld Whether the player just left the world; that path never flies.
	GlueArrival DecideGlueArrival(bool plazaReady, bool returningFromWorld);
}
