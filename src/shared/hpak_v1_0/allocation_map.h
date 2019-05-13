// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include <cstdint>
#include <vector>
#include <optional>

namespace mmo
{
	namespace hpak
	{
		struct AllocationMap
		{
			std::uintmax_t allocate(std::uintmax_t size);
			bool reserve(std::uintmax_t offset, std::uintmax_t size);
			std::uintmax_t getEnd() const;

		private:

			struct Entry
			{
				std::uintmax_t offset, size;
			};

			typedef std::vector<Entry> EntryVector;

			EntryVector m_entries;

			std::optional<std::uintmax_t> allocateImpl(
			    std::uintmax_t size,
			    std::uintmax_t begin,
			    std::uintmax_t end);
		};
	}
}
