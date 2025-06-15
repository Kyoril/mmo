// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
			OutgoingPacket(io::ISink &sink, bool proxy = false);

			void Start(uint16 id);
			void Finish();

		public:
			/// @brief Gets the id of this packet.
			[[nodiscard]] uint16 GetId() const { return m_id; }

			/// @brief Gets the size of this packet in bytes.
			[[nodiscard]] uint32 GetSize() const { return m_size; }

		private:
			uint16 m_id { 0 };
			uint32 m_size { 0 };
			size_t m_sizePos { 0 };
			size_t m_bodyPos { 0 };
			bool m_proxy { false };
		};
	}
}
