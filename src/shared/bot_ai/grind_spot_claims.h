// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cstddef>
#include <unordered_map>

namespace mmo
{
	/// Which bot works which grind spot, shared by the whole swarm.
	///
	/// The spot index is read-only and shared; this is its one mutable companion, so that bots
	/// spread over the world instead of every bot in a town picking the same nearest spawn. Like
	/// everything in the swarm it is single-threaded.
	class GrindSpotClaims final
	{
	public:
		/// Records that a bot now works a spot, replacing whatever it worked before.
		void Claim(uint32 botIndex, std::size_t spotIndex);

		/// Records that a bot works no spot.
		void Release(uint32 botIndex);

		/// How many bots work a spot.
		[[nodiscard]] uint32 GetCount(std::size_t spotIndex) const;

	private:
		std::unordered_map<uint32, std::size_t> m_spotByBot;
		std::unordered_map<std::size_t, uint32> m_countBySpot;
	};
}
