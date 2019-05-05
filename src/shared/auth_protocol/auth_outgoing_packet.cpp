// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "auth_outgoing_packet.h"

namespace mmo
{
	namespace auth
	{
		OutgoingPacket::OutgoingPacket(io::ISink &sink)
			: io::Writer(sink)
		{
		}

		void OutgoingPacket::Start(uint8 id)
		{
			*this << io::write<uint8>(id);
		}

		void OutgoingPacket::Finish()
		{
		}
	}
}
