// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "network/receive_state.h"
#include "binary_io/reader.h"
#include "binary_io/memory_source.h"

namespace mmo
{
	namespace auth
	{
		class IncomingPacket 
			: public io::Reader
		{
		public:

			IncomingPacket();
			
		public:
			[[nodiscard]] uint8 GetId() const { return m_id; }
			[[nodiscard]] uint32 GetSize() const { return m_size; }

			static ReceiveState Start(IncomingPacket &packet, io::MemorySource &source);

		private:

			uint8 m_id;
			uint32 m_size;
			io::MemorySource m_body;
		};
	}
}
