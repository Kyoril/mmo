// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "game_incoming_packet.h"

#include <limits>


namespace mmo
{
	namespace game
	{
		IncomingPacket::IncomingPacket()
			: m_id(std::numeric_limits<uint16>::max())
		{
		}

		uint16 IncomingPacket::GetId() const
		{
			return m_id;
		}

		ReceiveState IncomingPacket::Start(IncomingPacket &packet, io::MemorySource &source)
		{
			io::Reader streamReader(source);

			if (streamReader >> io::read<uint16>(packet.m_id))
			{
				std::size_t size = source.getRest();

				const char *const body = source.getPosition();
				source.skip(size);
				packet.m_body = io::MemorySource(body, body + size);
				packet.setSource(&packet.m_body);
				return receive_state::Complete;
			}

			return receive_state::Incomplete;
		}
	}
}
