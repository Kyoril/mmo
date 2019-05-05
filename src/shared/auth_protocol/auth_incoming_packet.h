// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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
			uint8 GetId() const;

			static ReceiveState Start(IncomingPacket &packet, io::MemorySource &source);

		private:

			uint8 m_id;
			io::MemorySource m_body;
		};
	}
}
