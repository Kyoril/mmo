// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "binary_io/writer.h"

namespace mmo
{
	namespace game
	{
		class OutgoingPacket 
			: public io::Writer
		{
		public:

			OutgoingPacket(io::ISink &sink);

			void Start(uint16 id);
			void Finish();

		private:
			size_t m_sizePos;
			size_t m_bodyPos;
		};
	}
}
