
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
	// ~6.5 yards. The loot distance is therefore set to cover that full range.
	constexpr float LootDistance = 6.5f;

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