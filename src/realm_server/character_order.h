// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <set>
#include <vector>

#include "base/typedefs.h"

namespace mmo
{
	/// Checks whether a client supplied character order is a permutation of the owned characters.
	/// @param requested The character guids in the order requested by the client.
	/// @param owned The guids of all characters of the account.
	/// @returns true if requested contains every owned guid exactly once and nothing else.
	bool IsValidCharacterOrder(const std::vector<uint64>& requested, const std::set<uint64>& owned);
}
