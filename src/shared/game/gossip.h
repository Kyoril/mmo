#pragma once

namespace mmo
{
	namespace gossip_actions
	{
		enum Type
		{
			/// Default value. Should not be used as this does simply nothing!
			None = 0,

			/// Shows the vendor menu of the npc.
			Vendor,

			/// Shows the trainer menu of the npc.
			Trainer,

			/// Shows another gossip menu. Can be used to add multi-paged gossip menus!
			GossipMenu,

			Count_
		};
	}

}