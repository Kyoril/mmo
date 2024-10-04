// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "auth_incoming_packet.h"
#include <limits>

#include "base/macros.h"

namespace mmo
{
	namespace auth
	{
		IncomingPacket::IncomingPacket()
			: m_id(std::numeric_limits<uint8>::max())
			, m_size(0)
		{
		}

		ReceiveState IncomingPacket::Start(IncomingPacket &packet, io::MemorySource &source)
		{
			io::Reader streamReader(source);

			if (streamReader
				>> io::read<uint8>(packet.m_id)
				>> io::read<uint32>(packet.m_size))
			{
				const auto size = source.getRest();
				if (size < packet.m_size)
				{
					return receive_state::Incomplete;
				}

				const char *const body = source.getPosition();
				const auto skipped = source.skip(packet.m_size);
				ASSERT(skipped == packet.m_size);
				
				packet.m_body = io::MemorySource(body, body + packet.m_size);
				packet.setSource(&packet.m_body);
				return receive_state::Complete;
			}

			return receive_state::Incomplete;
		}
	}
}
