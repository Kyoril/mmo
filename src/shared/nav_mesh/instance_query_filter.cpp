// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "instance_query_filter.h"

#include "DetourNavMesh.h"

#include <algorithm>

namespace mmo::nav
{
	namespace
	{
		bool lessRef(const std::pair<dtPolyRef, uint32>& entry, const dtPolyRef ref)
		{
			return entry.first < ref;
		}
	}

	bool InstanceQueryFilter::passFilter(const dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const
	{
		if (!dtQueryFilter::passFilter(ref, tile, poly))
		{
			return false;
		}

		// Only door polys can be blocked, so all other polys skip the lookup.
		if ((poly->flags & DoorPolyFlag) == 0 || m_blocked.empty())
		{
			return true;
		}

		return !IsBlocked(ref);
	}

	void InstanceQueryFilter::Block(const std::vector<dtPolyRef>& refs)
	{
		for (const dtPolyRef ref : refs)
		{
			const auto it = std::lower_bound(m_blocked.begin(), m_blocked.end(), ref, lessRef);
			if (it != m_blocked.end() && it->first == ref)
			{
				++it->second;
			}
			else
			{
				m_blocked.insert(it, { ref, 1 });
			}
		}
	}

	void InstanceQueryFilter::Unblock(const std::vector<dtPolyRef>& refs)
	{
		for (const dtPolyRef ref : refs)
		{
			const auto it = std::lower_bound(m_blocked.begin(), m_blocked.end(), ref, lessRef);
			if (it == m_blocked.end() || it->first != ref)
			{
				continue;
			}

			if (--it->second == 0)
			{
				m_blocked.erase(it);
			}
		}
	}

	bool InstanceQueryFilter::IsBlocked(const dtPolyRef ref) const
	{
		const auto it = std::lower_bound(m_blocked.begin(), m_blocked.end(), ref, lessRef);
		return it != m_blocked.end() && it->first == ref;
	}
}
