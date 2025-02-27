// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "binary_io/writer.h"

namespace mmo
{
	namespace auth
	{
		class OutgoingPacket final
			: public io::Writer
		{
		public:

			OutgoingPacket(io::ISink &sink);

			void Start(uint8 id);
			void Finish();

		private:
			size_t m_sizePos;
			size_t m_bodyPos;
		};
	}
}
