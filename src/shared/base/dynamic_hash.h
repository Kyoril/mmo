
#pragma once

namespace mmo
{
	class DynamicHash final
	{
	public:
		void Add(size_t value)
		{
			m_hash ^= value + 0x9e3779b9 + (value << 6) + (value >> 2);
		}

		operator size_t() const
		{
			return m_hash;
		}

	private:
		size_t m_hash = 0;
	};
}