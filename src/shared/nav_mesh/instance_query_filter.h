// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include "DetourNavMeshQuery.h"

#include <utility>
#include <vector>

namespace mmo::nav
{
	/// @brief Poly flag the nav builder sets on polys cut out under a door's footprint.
	/// @details Must match mmo::poly_flags::Door in nav_build/common.h.
	constexpr uint16 DoorPolyFlag = 1 << 5;

	/// @brief Query filter of one world instance: the default flag filter, plus the door polys of
	/// that instance's closed doors.
	/// @details The nav mesh is shared by every instance of a map, so door state cannot live in
	/// the mesh's poly flags. Blocks are reference counted, because overlapping door volumes can
	/// claim the same poly. Not thread safe: owned and used by the world thread.
	class InstanceQueryFilter final : public dtQueryFilter
	{
	public:
		/// @copydoc dtQueryFilter::passFilter
		bool passFilter(dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const override;

		/// @brief Blocks every given poly. A poly blocked n times needs n unblocks.
		void Block(const std::vector<dtPolyRef>& refs);

		/// @brief Releases one block of every given poly. Unknown refs are ignored.
		void Unblock(const std::vector<dtPolyRef>& refs);

		/// @brief Whether the poly is blocked at least once.
		[[nodiscard]] bool IsBlocked(dtPolyRef ref) const;

		/// @brief Number of distinct blocked polys.
		[[nodiscard]] size_t GetBlockedCount() const { return m_blocked.size(); }

	private:
		/// Blocked polys and their block count, sorted by ref.
		std::vector<std::pair<dtPolyRef, uint32>> m_blocked;
	};
}
