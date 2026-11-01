// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "base_terrain_sampler.h"

#include "pcg/graph/hashing.h"

#include <cmath>

namespace mmo
{
	namespace
	{
		uint64 PackVertex(const int64 vx, const int64 vz)
		{
			return (static_cast<uint64>(static_cast<uint32>(vx)) << 32) | static_cast<uint64>(static_cast<uint32>(vz));
		}
	}

	BaseTerrainSampler::BaseTerrainSampler(const pcg::graph::ITerrainSampler& live, const pcg::TerrainContribution& contribution,
		const pcg::GridLayout& layout)
		: m_live(live)
		, m_layout(layout)
		, m_contributionHash(HashContribution(contribution))
	{
		m_changes.reserve(contribution.outer.size());
		for (const pcg::TerrainContribution::Entry<float>& entry : contribution.outer)
		{
			const float change = entry.written - entry.base;
			if (change != 0.0f && std::isfinite(change))
			{
				m_changes[PackVertex(entry.x, entry.z)] = change;
			}
		}
	}

	uint64 BaseTerrainSampler::HashContribution(const pcg::TerrainContribution& contribution)
	{
		pcg::graph::Hasher hasher;
		hasher.U64(contribution.outer.size());
		for (const pcg::TerrainContribution::Entry<float>& entry : contribution.outer)
		{
			hasher.U64(static_cast<uint64>(static_cast<uint32>(entry.x)));
			hasher.U64(static_cast<uint64>(static_cast<uint32>(entry.z)));
			hasher.F32(entry.base);
			hasher.F32(entry.written);
		}
		return hasher.Result();
	}

	bool BaseTerrainSampler::Height(const float x, const float z, float& h) const
	{
		if (!m_live.Height(x, z, h))
		{
			return false;
		}

		h -= GetHeightChange(x, z);
		return true;
	}

	Vector3 BaseTerrainSampler::Normal(const float x, const float z) const
	{
		const float e = static_cast<float>(m_layout.vertexSpacing * 0.5);
		if (m_changes.empty() || !(e > 0.0f))
		{
			return m_live.Normal(x, z);
		}

		const bool changed = GetHeightChange(x, z) != 0.0f || GetHeightChange(x - e, z) != 0.0f || GetHeightChange(x + e, z) != 0.0f ||
			GetHeightChange(x, z - e) != 0.0f || GetHeightChange(x, z + e) != 0.0f;
		float left = 0.0f, right = 0.0f, down = 0.0f, up = 0.0f;
		if (!changed || !Height(x - e, z, left) || !Height(x + e, z, right) || !Height(x, z - e, down) || !Height(x, z + e, up))
		{
			return m_live.Normal(x, z);
		}

		// y = h(x, z): the normal is (-dh/dx, 1, -dh/dz), scaled by 2e.
		return Vector3(left - right, 2.0f * e, down - up).NormalizedCopy();
	}

	float BaseTerrainSampler::SplatWeight(const String& textureKey, const float x, const float z) const
	{
		return m_live.SplatWeight(textureKey, x, z);
	}

	bool BaseTerrainSampler::WaterLevel(const float x, const float z, float& level) const
	{
		return m_live.WaterLevel(x, z, level);
	}

	uint64 BaseTerrainSampler::Revision() const
	{
		pcg::graph::Hasher hasher;
		hasher.U64(m_live.Revision());
		hasher.U64(m_contributionHash);
		return hasher.Result();
	}

	float BaseTerrainSampler::GetHeightChange(const float x, const float z) const
	{
		if (m_changes.empty() || !(m_layout.vertexSpacing > 0.0))
		{
			return 0.0f;
		}

		const double fx = (static_cast<double>(x) - m_layout.originX) / m_layout.vertexSpacing;
		const double fz = (static_cast<double>(z) - m_layout.originZ) / m_layout.vertexSpacing;
		if (!std::isfinite(fx) || !std::isfinite(fz) || std::abs(fx) > 1.0e9 || std::abs(fz) > 1.0e9)
		{
			return 0.0f;
		}

		const double cellX = std::floor(fx);
		const double cellZ = std::floor(fz);
		const double tx = fx - cellX;
		const double tz = fz - cellZ;
		const int64 vx = static_cast<int64>(cellX);
		const int64 vz = static_cast<int64>(cellZ);

		const double change = (1.0 - tx) * (1.0 - tz) * GetVertexChange(vx, vz) + tx * (1.0 - tz) * GetVertexChange(vx + 1, vz) +
			(1.0 - tx) * tz * GetVertexChange(vx, vz + 1) + tx * tz * GetVertexChange(vx + 1, vz + 1);
		return static_cast<float>(change);
	}

	float BaseTerrainSampler::GetVertexChange(const int64 vx, const int64 vz) const
	{
		const auto it = m_changes.find(PackVertex(vx, vz));
		return it != m_changes.end() ? it->second : 0.0f;
	}
}
