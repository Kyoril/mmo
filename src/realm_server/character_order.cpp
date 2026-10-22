// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_order.h"

namespace mmo
{
	bool IsValidCharacterOrder(const std::vector<uint64>& requested, const std::set<uint64>& owned)
	{
		if (requested.size() != owned.size())
		{
			return false;
		}

		std::set<uint64> seen;
		for (const uint64 guid : requested)
		{
			if (owned.find(guid) == owned.end() || !seen.insert(guid).second)
			{
				return false;
			}
		}

		return true;
	}
}
