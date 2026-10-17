
#pragma once

#include "base/typedefs.h"

namespace mmo
{
	// Loot / interaction distance.
	//
	// This must be at least as large as the maximum melee auto-attack engagement
	// range, otherwise a creature can chase and attack the player from further
	// away than the player is allowed to loot it once it dies, producing spurious
	// "too far away to loot" errors for a corpse the player was just fighting.
	//
	// A default-scale creature and player each have a melee reach of ~2.5 yards
	// (see GameUnitS::GetMeleeReach), giving a combined reach of ~5.0 yards. While
	// both units are moving the auto-attack range is additionally widened by
	// MELEE_CHASE_RANGE_BONUS (1.5 yards), so a creature can land hits from up to
	// ~6.5 yards, which this distance has to cover.
	//
	// It must also be at least as large as the range of the "Open" spell family
	// (gather nodes, chests, doors), which is the data-driven Melee Range of 7.0
	// yards (range type 6 in data/editor/data/ranges.data). Both the client and the
	// server validate such a cast against the spell's range, so a node presented as
	// gatherable at up to 7.0 yards must still be within reach for the interaction
	// the finished cast performs - otherwise the gather cast runs its full duration
	// and the loot window is silently refused.
	constexpr float LootDistance = 7.0f;

	namespace loot_error
	{
		enum Type
		{
			/// You don't have permission to loot that corpse.
			DidntKill = 0,

			/// You are too far away to loot that corpse.
			TooFar,

			/// You must be facing the corpse to loot it.
			BadFacing,

			/// Someone is already looting that corpse.
			Locked,

			/// You need to be standing up to loot something!
			NotStanding,

			/// You can't loot anything while stunned!
			Stunned,

			/// Player not found.
			PlayerNotFound,

			/// That player's inventory is full.
			MasterInvFull,

			/// Player has too many of that item already.
			MasterUniqueItem,

			/// Can't assign item to that player.
			MasterOther
		};
	}

	namespace loot_type
	{
		enum Type
		{
			/// No loot type
			None = 0,

			/// Corpse loot (dead creatures).
			Corpse = 1
		};
	}

}