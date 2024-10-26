
#pragma once

namespace mmo
{

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