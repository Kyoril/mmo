// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "point_prune.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace mmo::pcg::graph
{
	namespace
	{
		enum class State : uint8
		{
			Undecided,
			Kept,
			Removed
		};

		struct CellEntry
		{
			int64 cz;
			int64 cx;
			uint32 index;

			bool operator<(const CellEntry& o) const
			{
				return std::tie(cz, cx, index) < std::tie(o.cz, o.cx, o.index);
			}
		};

		/// Grid cell of a coordinate; clamped so huge coordinates cannot overflow.
		int64 CellOf(const float v, const float size)
		{
			const double c = std::floor(static_cast<double>(v) / static_cast<double>(size));
			return static_cast<int64>(std::clamp(c, -4.0e18, 4.0e18));
		}

		bool Finite(const Vector3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}
	}

	std::vector<uint8> PruneByDistance(const std::vector<Point>& points, const PruneOptions& options, const std::vector<float>* priorities)
	{
		const size_t n = points.size();
		std::vector<uint8> keep(n, 1);
		const float r = options.minSpacing;
		if (n < 2 || !(r > 0.0f) || !std::isfinite(r))
		{
			return keep;
		}
		if (priorities != nullptr && priorities->size() != n)
		{
			priorities = nullptr;
		}

		// Sorted grid of cell size r: conflicting points are at most one cell apart on each axis. Neighbours are
		// found on the fly from the grid, so memory stays O(n) however many neighbours a point has.
		constexpr uint32 NoCell = 0xFFFFFFFFu;
		std::vector<CellEntry> grid;
		grid.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			const Vector3& p = points[i].position;
			if (Finite(p))
			{
				grid.push_back(CellEntry{ CellOf(p.z, r), CellOf(p.x, r), static_cast<uint32>(i) });
			}
		}
		std::sort(grid.begin(), grid.end());

		// Position of each point's entry in the grid (NoCell for non-finite points, which never conflict).
		std::vector<uint32> gridPos(n, NoCell);
		for (size_t k = 0; k < grid.size(); ++k)
		{
			gridPos[grid[k].index] = static_cast<uint32>(k);
		}

		const float r2 = r * r;

		// Calls visit(j) for every point j that conflicts with point i until visit returns true; returns whether it
		// did. The order of the neighbours does not matter to any caller.
		const auto anyNeighbour = [&](const uint32 i, const auto& visit)
		{
			if (gridPos[i] == NoCell)
			{
				return false;
			}
			const CellEntry& e = grid[gridPos[i]];
			const Vector3& a = points[i].position;
			for (int64 dz = -1; dz <= 1; ++dz)
			{
				for (int64 dx = -1; dx <= 1; ++dx)
				{
					const CellEntry lo{ e.cz + dz, e.cx + dx, 0 };
					for (auto it = std::lower_bound(grid.begin(), grid.end(), lo); it != grid.end() && it->cz == lo.cz && it->cx == lo.cx; ++it)
					{
						if (it->index == i)
						{
							continue;
						}
						const Vector3& b = points[it->index].position;
						const float ddx = a.x - b.x;
						const float ddz = a.z - b.z;
						const float ddy = options.planar ? 0.0f : a.y - b.y;
						if (ddx * ddx + ddy * ddy + ddz * ddz < r2 && visit(it->index))
						{
							return true;
						}
					}
				}
			}
			return false;
		};

		const auto outranks = [&](const uint32 a, const uint32 b)
		{
			if (priorities != nullptr && (*priorities)[a] != (*priorities)[b])
			{
				return (*priorities)[a] > (*priorities)[b];
			}
			if (points[a].seed != points[b].seed)
			{
				return points[a].seed > points[b].seed;
			}
			return a < b;
		};

		std::vector<State> state(n, State::Undecided);
		std::vector<uint32> chosen;
		for (uint32 round = 0; round < options.rounds; ++round)
		{
			chosen.clear();
			for (uint32 i = 0; i < n; ++i)
			{
				if (state[i] != State::Undecided)
				{
					continue;
				}
				const bool beaten = anyNeighbour(i, [&](const uint32 j)
				{
					return state[j] == State::Undecided && outranks(j, i);
				});
				if (!beaten)
				{
					chosen.push_back(i);
				}
			}
			if (chosen.empty())
			{
				break;
			}
			for (const uint32 i : chosen)
			{
				state[i] = State::Kept;
			}
			for (const uint32 i : chosen)
			{
				anyNeighbour(i, [&](const uint32 j)
				{
					if (state[j] == State::Undecided)
					{
						state[j] = State::Removed;
					}
					return false;
				});
			}
		}

		// Final pass; it reads the states and only writes the keep mask.
		for (uint32 i = 0; i < n; ++i)
		{
			if (state[i] == State::Kept)
			{
				continue;
			}
			if (state[i] == State::Removed)
			{
				keep[i] = 0;
				continue;
			}
			const bool beaten = anyNeighbour(i, [&](const uint32 j)
			{
				return state[j] == State::Kept || (state[j] == State::Undecided && outranks(j, i));
			});
			if (beaten)
			{
				keep[i] = 0;
			}
		}
		return keep;
	}
}
