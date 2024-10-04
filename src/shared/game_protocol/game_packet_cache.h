// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "game_outgoing_packet.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include <vector>
#include <cassert>

namespace mmo
{
	namespace game
	{
		template <class F>
		class PacketCache
		{
		public:

			explicit PacketCache(F createPacket)
				: m_createPacket(createPacket)
			{
			}

			void CopyToSink(io::ISink &sink)
			{
				if (m_buffer.empty())
				{
					io::VectorSink bufferSink(m_buffer);
					mmo::auth::OutgoingPacket packet(bufferSink);
					m_createPacket(packet);
					assert(!m_buffer.empty());
				}

				io::Writer sinkWriter(sink);
				sinkWriter << io::write_range(m_buffer);
				sink.Flush();
			}

		private:

			F m_createPacket;
			std::vector<char> m_buffer;
		};

		template <class F>
		PacketCache<F> MakePacketCache(F createPacket)
		{
			return PacketCache<F>(createPacket);
		}
	}
}
