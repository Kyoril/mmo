// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_outgoing_packet.h"


namespace mmo
{
	namespace game
	{
		OutgoingPacket::OutgoingPacket(io::ISink &sink, const bool proxy)
			: io::Writer(sink)
			, m_proxy(proxy)
		{
		}

		void OutgoingPacket::Start(const uint16 id)
		{
			m_id = id;
			
			if (!m_proxy)
			{
				*this
					<< io::write<uint16>(id);

				m_sizePos = Sink().Position();
				*this
					<< io::write<uint32>(0);

				m_bodyPos = Sink().Position();
			}
		}

		void OutgoingPacket::Finish()
		{
			if (!m_proxy)
			{
				const size_t endPos = Sink().Position();

				m_size = endPos - m_bodyPos;
				Sink().Overwrite(m_sizePos, reinterpret_cast<const char*>(&m_size), sizeof(m_size));
			}
		}
	}
}
