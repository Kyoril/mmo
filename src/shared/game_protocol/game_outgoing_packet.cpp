// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "game_outgoing_packet.h"


namespace mmo
{
	namespace game
	{
		OutgoingPacket::OutgoingPacket(io::ISink &sink)
			: io::Writer(sink)
			, m_sizePos(0)
			, m_bodyPos(0)
		{
		}

		void OutgoingPacket::Start(uint16 id)
		{
			*this
				<< io::write<uint16>(id);

			m_sizePos = Sink().Position();
			*this
				<< io::write<uint32>(0);

			m_bodyPos = Sink().Position();
		}

		void OutgoingPacket::Finish()
		{
			const size_t endPos = Sink().Position();

			const uint32 packetSize = endPos - m_bodyPos;
			Sink().Overwrite(m_sizePos, reinterpret_cast<const char*>(&packetSize), sizeof(packetSize));
		}
	}
}
