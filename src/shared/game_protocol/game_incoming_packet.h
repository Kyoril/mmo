// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "network/receive_state.h"
#include "binary_io/reader.h"
#include "binary_io/memory_source.h"


namespace mmo
{
	namespace game
	{
		class IncomingPacket 
			: public io::Reader
		{
		public:

			IncomingPacket();

			inline uint16 GetId() const { return m_id; }
			inline uint32 GetSize() const { return m_size; }

			static ReceiveState Start(IncomingPacket &packet, io::MemorySource &source);

		private:

			uint16 m_id;
			uint32 m_size;
			io::MemorySource m_body;
		};
	}
}
