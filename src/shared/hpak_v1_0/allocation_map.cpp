// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "allocation_map.h"
#include "base/macros.h"

#include <limits>


namespace mmo
{
	namespace hpak
	{
		std::uintmax_t AllocationMap::Allocate(std::uintmax_t size)
		{
			return *AllocateImpl(
			           size,
			           0,
			           std::numeric_limits<std::uintmax_t>::max());
		}

		bool AllocationMap::Reserve(std::uintmax_t offset, std::uintmax_t size)
		{
			return AllocateImpl(
			           size,
			           offset,
			           offset + 1).has_value();
		}

		std::uintmax_t AllocationMap::GetEnd() const
		{
			if (m_entries.empty())
			{
				return 0;
			}
			
			const auto &back = m_entries.back();
			return (back.offset + back.size);
		}


		std::optional<std::uintmax_t> AllocationMap::AllocateImpl(
		    std::uintmax_t size,
		    std::uintmax_t begin,
		    std::uintmax_t end)
		{
			ASSERT(begin <= end);

			std::optional<std::uintmax_t> result;

			std::uintmax_t beginOfFreeSpace = begin;
			auto i = m_entries.begin();
			for (; (i != m_entries.end()) && (beginOfFreeSpace < end); ++i)
			{
				if (i->offset + i->size < beginOfFreeSpace)
				{
					continue;
				}

				if (beginOfFreeSpace + size <= i->offset)
				{
					break;
				}

				beginOfFreeSpace = (i->offset + i->size);
			}

			if (beginOfFreeSpace >= end)
			{
				return result;
			}

			Entry newEntry{};
			newEntry.offset = beginOfFreeSpace;
			newEntry.size = size;
			m_entries.insert(i, newEntry);

			result = newEntry.offset;
			return result;
		}
	}
}
