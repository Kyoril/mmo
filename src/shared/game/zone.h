#pragma once

namespace mmo
{
	namespace zone_flags
	{
		enum Type
		{
			None = 0,

			/// This zone allows players to rest and log out immediately.
			AllowResting = 1 << 0,

			/// This zone allows player duels.
			AllowDueling = 1 << 1,

			/// This zone allows free-for-all PvP, which means every player can attack every other player except for party and raid members.
			FreeForAllPvP = 1 << 2,

			/// This zone is a contested area, which means players are flagged for pvp when entering.
			Contested = 1 << 3,
		};
	}
}