// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "polyline_index.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg::graph
{
	namespace
	{
		/// Largest grid size (cells per axis) of a PolylineIndex.
		constexpr double kMaxIndexCells = 256.0;
	}

	void PolylineIndex::Build(const std::vector<SplineSample>& samples, const bool closed, const float reach, const std::vector<uint8>& skipSegments)
	{
		m_samples = samples;
		m_closed = closed;
		m_reach = std::max(0.0f, reach);
		m_segments.clear();
		m_cellStart.clear();
		m_cells.clear();
		m_columns = 0;
		m_rows = 0;
		if (m_samples.empty())
		{
			return;
		}

		const uint32 n = static_cast<uint32>(m_samples.size());
		if (n == 1)
		{
			m_segments.emplace_back(0, 0);
		}
		const auto skipped = [&skipSegments](const size_t s)
		{
			return s < skipSegments.size() && skipSegments[s] != 0;
		};
		for (uint32 i = 0; i + 1 < n; ++i)
		{
			if (!skipped(i))
			{
				m_segments.emplace_back(i, i + 1);
			}
		}
		if (closed && n >= 3 && !skipped(n - 1))
		{
			m_segments.emplace_back(n - 1, 0);
		}

		double minX = m_samples[0].x;
		double maxX = minX;
		double minZ = m_samples[0].z;
		double maxZ = minZ;
		for (const SplineSample& s : m_samples)
		{
			minX = std::min(minX, static_cast<double>(s.x));
			maxX = std::max(maxX, static_cast<double>(s.x));
			minZ = std::min(minZ, static_cast<double>(s.z));
			maxZ = std::max(maxZ, static_cast<double>(s.z));
		}
		minX -= m_reach;
		minZ -= m_reach;
		maxX += m_reach;
		maxZ += m_reach;
		const double extent = std::max(maxX - minX, maxZ - minZ);
		m_cellSize = std::max({ static_cast<double>(m_reach), extent / kMaxIndexCells, 0.5 });
		m_originX = minX;
		m_originZ = minZ;
		m_columns = static_cast<int64>(std::floor((maxX - minX) / m_cellSize)) + 1;
		m_rows = static_cast<int64>(std::floor((maxZ - minZ) / m_cellSize)) + 1;

		// Two passes: count, then fill (CSR, segments ascending per cell).
		const size_t cellCount = static_cast<size_t>(m_columns * m_rows);
		std::vector<uint32> counts(cellCount, 0);
		for (int pass = 0; pass < 2; ++pass)
		{
			std::vector<uint32> fill;
			if (pass == 1)
			{
				m_cellStart.assign(cellCount + 1, 0);
				for (size_t c = 0; c < cellCount; ++c)
				{
					m_cellStart[c + 1] = m_cellStart[c] + counts[c];
				}
				m_cells.resize(m_cellStart[cellCount]);
				fill.assign(m_cellStart.begin(), m_cellStart.end() - 1);
			}
			for (uint32 s = 0; s < static_cast<uint32>(m_segments.size()); ++s)
			{
				const SplineSample& a = m_samples[m_segments[s].first];
				const SplineSample& b = m_samples[m_segments[s].second];
				const int64 x0 = CellOf(std::min(a.x, b.x) - m_reach, m_originX, m_columns);
				const int64 x1 = CellOf(std::max(a.x, b.x) + m_reach, m_originX, m_columns);
				const int64 z0 = CellOf(std::min(a.z, b.z) - m_reach, m_originZ, m_rows);
				const int64 z1 = CellOf(std::max(a.z, b.z) + m_reach, m_originZ, m_rows);
				for (int64 z = z0; z <= z1; ++z)
				{
					for (int64 x = x0; x <= x1; ++x)
					{
						const size_t c = static_cast<size_t>(z * m_columns + x);
						if (pass == 0)
						{
							++counts[c];
						}
						else
						{
							m_cells[fill[c]++] = s;
						}
					}
				}
			}
		}
	}

	bool PolylineIndex::Nearest(const float x, const float z, NearestHit& hit) const
	{
		if (m_columns == 0)
		{
			return false;
		}
		const double cx = std::floor((x - m_originX) / m_cellSize);
		const double cz = std::floor((z - m_originZ) / m_cellSize);
		if (!(cx >= 0.0 && cz >= 0.0 && cx < static_cast<double>(m_columns) && cz < static_cast<double>(m_rows)))
		{
			return false;
		}
		const size_t c = static_cast<size_t>(static_cast<int64>(cz) * m_columns + static_cast<int64>(cx));
		bool found = false;
		double best = 0.0;
		for (uint32 i = m_cellStart[c]; i < m_cellStart[c + 1]; ++i)
		{
			const std::pair<uint32, uint32>& seg = m_segments[m_cells[i]];
			const SplineSample& a = m_samples[seg.first];
			const SplineSample& b = m_samples[seg.second];
			const double dx = static_cast<double>(b.x) - a.x;
			const double dz = static_cast<double>(b.z) - a.z;
			const double len2 = dx * dx + dz * dz;
			double t = 0.0;
			if (len2 > 0.0)
			{
				t = std::min(1.0, std::max(0.0, ((x - a.x) * dx + (z - a.z) * dz) / len2));
			}
			const double px = x - (a.x + dx * t);
			const double pz = z - (a.z + dz * t);
			const double d = std::sqrt(px * px + pz * pz);
			if (d <= m_reach && (!found || d < best))
			{
				found = true;
				best = d;
				hit.a = seg.first;
				hit.b = seg.second;
				hit.t = static_cast<float>(t);
			}
		}
		hit.distance = static_cast<float>(best);
		return found;
	}

	int64 PolylineIndex::CellOf(const double v, const double origin, const int64 count) const
	{
		const int64 c = static_cast<int64>(std::floor((v - origin) / m_cellSize));
		return std::min(count - 1, std::max<int64>(0, c));
	}
}
