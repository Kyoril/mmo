// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "game_outgoing_packet.h"


namespace mmo
{
	namespace game
	{
		OutgoingPacket::OutgoingPacket(io::ISink &sink)
			: io::Writer(sink)
		{
		}

		void OutgoingPacket::Start(uint16 id)
		{
			*this << io::write<uint16>(id);
		}

		void OutgoingPacket::Finish()
		{
		}
	}
}
