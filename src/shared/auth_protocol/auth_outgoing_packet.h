// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "binary_io/writer.h"

namespace mmo
{
	namespace auth
	{
		class OutgoingPacket 
			: public io::Writer
		{
		public:

			OutgoingPacket(io::ISink &sink);

			void Start(uint8 id);
			void Finish();
		};
	}
}
