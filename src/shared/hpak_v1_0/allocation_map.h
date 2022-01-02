// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
			std::uintmax_t Allocate(std::uintmax_t size);
			bool Reserve(std::uintmax_t offset, std::uintmax_t size);
			[[nodiscard]] std::uintmax_t GetEnd() const;

		private:

			struct Entry
			{
				std::uintmax_t offset, size;
			};

			typedef std::vector<Entry> EntryVector;

			EntryVector m_entries;

			std::optional<std::uintmax_t> AllocateImpl(
			    std::uintmax_t size,
			    std::uintmax_t begin,
			    std::uintmax_t end);
		};
	}
}
