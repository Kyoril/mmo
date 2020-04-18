// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "auth_outgoing_packet.h"

namespace mmo
{
	namespace auth
	{
		OutgoingPacket::OutgoingPacket(io::ISink &sink)
			: io::Writer(sink)
			, m_sizePos(0)
			, m_bodyPos(0)
		{
		}

		void OutgoingPacket::Start(uint8 id)
		{
			*this << io::write<uint8>(id);

			m_sizePos = sink().position();
			*this
				<< io::write<uint32>(0);

			m_bodyPos = sink().position();
		}

		void OutgoingPacket::Finish()
		{
			const size_t endPos = sink().position();

			const uint32 packetSize = endPos - m_bodyPos;
			sink().overwrite(m_sizePos, reinterpret_cast<const char*>(&packetSize), sizeof(packetSize));
		}
	}
}
