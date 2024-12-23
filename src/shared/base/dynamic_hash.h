// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class DynamicHash final
	{
	public:
		void Add64(uint64 value)
		{
			m_hash ^= value + 0x9e3779b9 + (value << 6) + (value >> 2);
		}

		void Add32(uint32 value)
		{
			Add64(static_cast<size_t>(value));
		}

		void AddFloat(float value)
		{
			static_assert(sizeof(FLOAT) == sizeof(uint32));
			Add32(*reinterpret_cast<uint32*>(&value));
		}

		operator size_t() const
		{
			return m_hash;
		}

	private:
		size_t m_hash = 0;
	};
}