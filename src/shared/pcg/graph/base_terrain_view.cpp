// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "base_terrain_view.h"

#include "hashing.h"

#include "math/math_utils.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace mmo::pcg::graph
{
	namespace
	{
		int64 FloorDiv(const int64 a, const int64 b)
		{
			const int64 q = a / b;
			return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
		}

		template<typename T>
		void HashEntries(Hasher& hasher, const std::vector<TerrainContribution::Entry<T>>& entries)
		{
			hasher.U64(entries.size());
			for (const auto& e : entries)
			{
				hasher.U64(static_cast<uint64>(static_cast<uint32>(e.x)));
				hasher.U64(static_cast<uint64>(static_cast<uint32>(e.z)));
				if constexpr (std::is_same_v<T, float>)
				{
					hasher.F32(e.base);
					hasher.F32(e.written);
				}
				else if constexpr (std::is_same_v<T, String>)
				{
					hasher.Str(e.base);
					hasher.Str(e.written);
				}
				else
				{
					hasher.U64(static_cast<uint64>(e.base));
					hasher.U64(static_cast<uint64>(e.written));
				}
			}
		}
	}

	BaseTerrainView::BaseTerrainView(const ITerrainSampler& live, const ITerrainAccess& access, const TerrainContribution& contribution,
		LayerMatchFunction layers, const int32 vertexStepsPerPage)
		: m_live(live)
		, m_access(access)
		, m_layers(std::move(layers))
		, m_layout(access.GetLayout())
		, m_stepsPerPage(std::max(1, vertexStepsPerPage))
		, m_materialChanges(!contribution.materials.empty())
		, m_contributionHash(HashContribution(contribution))
	{
		for (const auto& e : contribution.outer)
		{
			m_outer[Pack(e.x, e.z)] = Recorded<float>{ e.base, e.written };
		}
		for (const auto& e : contribution.waterHeights)
		{
			m_waterHeights[Pack(e.x, e.z)] = Recorded<float>{ e.base, e.written };
		}
		for (const auto& e : contribution.splat)
		{
			m_splat[Pack(e.x, e.z)] = Recorded<uint32>{ e.base, e.written };
		}
		for (const auto& e : contribution.waterQuads)
		{
			m_waterQuads[Pack(e.x, e.z)] = Recorded<uint8>{ e.base, e.written };
		}
	}

	uint64 BaseTerrainView::HashContribution(const TerrainContribution& contribution)
	{
		Hasher hasher;
		HashEntries(hasher, contribution.outer);
		HashEntries(hasher, contribution.inner);
		HashEntries(hasher, contribution.waterHeights);
		HashEntries(hasher, contribution.splat);
		HashEntries(hasher, contribution.waterQuads);
		HashEntries(hasher, contribution.materials);
		return hasher.Result();
	}

	BaseTerrainView::Key BaseTerrainView::Pack(const int64 x, const int64 z)
	{
		return (static_cast<uint64>(static_cast<uint32>(x)) << 32) | static_cast<uint64>(static_cast<uint32>(z));
	}

	bool BaseTerrainView::Cell(const float x, const float z, int64& vx, int64& vz, float& fx, float& fz) const
	{
		return GridCell(x, z, m_layout.vertexSpacing, vx, vz, fx, fz);
	}

	bool BaseTerrainView::GridCell(const float x, const float z, const double spacing, int64& vx, int64& vz, float& fx, float& fz) const
	{
		if (!(spacing > 0.0))
		{
			return false;
		}
		const double gx = (static_cast<double>(x) - m_layout.originX) / spacing;
		const double gz = (static_cast<double>(z) - m_layout.originZ) / spacing;
		if (!std::isfinite(gx) || !std::isfinite(gz) || std::abs(gx) > 1.0e9 || std::abs(gz) > 1.0e9)
		{
			return false;
		}
		const double cx = std::floor(gx);
		const double cz = std::floor(gz);
		vx = static_cast<int64>(cx);
		vz = static_cast<int64>(cz);
		fx = static_cast<float>(gx - cx);
		fz = static_cast<float>(gz - cz);
		return true;
	}

	float BaseTerrainView::OuterChange(const int64 vx, const int64 vz) const
	{
		// live - base: written - base where live == written, and also where it differs (Unapply keeps the user's change).
		const auto it = m_outer.find(Pack(vx, vz));
		return it != m_outer.end() ? it->second.written - it->second.base : 0.0f;
	}

	float BaseTerrainView::BaseOuterHeight(const int64 vx, const int64 vz) const
	{
		const float live = m_access.GetOuterHeight(static_cast<int32>(vx), static_cast<int32>(vz));
		const auto it = m_outer.find(Pack(vx, vz));
		if (it == m_outer.end())
		{
			return live;
		}
		return live == it->second.written ? it->second.base : live - (it->second.written - it->second.base);
	}

	float BaseTerrainView::BaseWaterHeight(const int64 vx, const int64 vz) const
	{
		const float live = m_access.GetWaterHeight(static_cast<int32>(vx), static_cast<int32>(vz));
		const auto it = m_waterHeights.find(Pack(vx, vz));
		if (it == m_waterHeights.end())
		{
			return live;
		}
		return live == it->second.written ? it->second.base : live - (it->second.written - it->second.base);
	}

	float BaseTerrainView::GetHeightChange(const float x, const float z) const
	{
		int64 vx = 0, vz = 0;
		float fx = 0.0f, fz = 0.0f;
		if (m_outer.empty() || !Cell(x, z, vx, vz, fx, fz))
		{
			return 0.0f;
		}

		const double tx = fx;
		const double tz = fz;
		const double change = (1.0 - tx) * (1.0 - tz) * OuterChange(vx, vz) + tx * (1.0 - tz) * OuterChange(vx + 1, vz) +
			(1.0 - tx) * tz * OuterChange(vx, vz + 1) + tx * tz * OuterChange(vx + 1, vz + 1);
		return static_cast<float>(change);
	}

	bool BaseTerrainView::Height(const float x, const float z, float& h) const
	{
		if (!m_live.Height(x, z, h))
		{
			return false;
		}
		h -= GetHeightChange(x, z);
		return true;
	}

	Vector3 BaseTerrainView::BaseVertexNormal(const int64 vx, const int64 vz, const int64 pageStartX, const int64 pageStartZ) const
	{
		// Page::CalculateNormalAt: no left / up neighbour at page-local index 0, scaling PageSize / OuterVerticesPerPageSide.
		const float scaling = static_cast<float>(m_layout.vertexSpacing * m_stepsPerPage / (m_stepsPerPage + 1.0));
		const float center = BaseOuterHeight(vx, vz);
		const float left = vx - pageStartX > 0 ? BaseOuterHeight(vx - 1, vz) : center;
		const float right = BaseOuterHeight(vx + 1, vz);
		const float up = vz - pageStartZ > 0 ? BaseOuterHeight(vx, vz - 1) : center;
		const float down = BaseOuterHeight(vx, vz + 1);

		const float dx = (right - left) / (2.0f * scaling);
		const float dz = (down - up) / (2.0f * scaling);
		Vector3 normal(-dx, 1.0f, -dz);
		normal.Normalize();

		// Stored normals are SNorm8.
		const EncodedNormal8 encoded = EncodeNormalSNorm8(normal.x, normal.y, normal.z);
		Vector3 decoded;
		DecodeNormalSNorm8(encoded, decoded.x, decoded.y, decoded.z);
		return decoded;
	}

	Vector3 BaseTerrainView::Normal(const float x, const float z) const
	{
		int64 vx = 0, vz = 0;
		float fx = 0.0f, fz = 0.0f;
		if (m_outer.empty() || !Cell(x, z, vx, vz, fx, fz))
		{
			return m_live.Normal(x, z);
		}

		// The corner normals read the corners and their direct neighbours.
		bool recorded = false;
		for (int64 dz = -1; dz <= 2 && !recorded; ++dz)
		{
			for (int64 dx = -1; dx <= 2 && !recorded; ++dx)
			{
				recorded = m_outer.count(Pack(vx + dx, vz + dz)) != 0;
			}
		}
		float h = 0.0f;
		if (!recorded || !m_live.Height(x, z, h))
		{
			return m_live.Normal(x, z);
		}

		const int64 pageStartX = FloorDiv(vx, m_stepsPerPage) * m_stepsPerPage;
		const int64 pageStartZ = FloorDiv(vz, m_stepsPerPage) * m_stepsPerPage;
		const Vector3 n00 = BaseVertexNormal(vx, vz, pageStartX, pageStartZ);
		const Vector3 n01 = BaseVertexNormal(vx, vz + 1, pageStartX, pageStartZ);
		const Vector3 n10 = BaseVertexNormal(vx + 1, vz, pageStartX, pageStartZ);
		const Vector3 n11 = BaseVertexNormal(vx + 1, vz + 1, pageStartX, pageStartZ);
		const Vector3 n0 = n00 * (1.0f - fx) + n10 * fx;
		const Vector3 n1 = n01 * (1.0f - fx) + n11 * fx;
		return (n0 * (1.0f - fz) + n1 * fz).NormalizedCopy();
	}

	float BaseTerrainView::SplatWeight(const String& textureKey, const float x, const float z) const
	{
		if (m_splat.empty() || textureKey.empty() || !(m_layout.pixelSpacing > 0.0))
		{
			return m_live.SplatWeight(textureKey, x, z);
		}

		// Terrain::GetLayerValueAt: the pixel truncated from the position in float.
		const float px = (x - static_cast<float>(m_layout.originX)) / static_cast<float>(m_layout.pixelSpacing);
		const float pz = (z - static_cast<float>(m_layout.originZ)) / static_cast<float>(m_layout.pixelSpacing);
		if (!std::isfinite(px) || !std::isfinite(pz) || std::abs(px) > 2.0e9f || std::abs(pz) > 2.0e9f)
		{
			return m_live.SplatWeight(textureKey, x, z);
		}
		const int32 ix = static_cast<int32>(px);
		const int32 iz = static_cast<int32>(pz);
		const auto it = ix >= 0 && iz >= 0 ? m_splat.find(Pack(ix, iz)) : m_splat.end();
		if (it == m_splat.end())
		{
			return m_live.SplatWeight(textureKey, x, z);
		}

		const uint32 live = m_access.GetSplat(ix, iz);
		const uint32 packed = live == it->second.written ? it->second.base : live;
		const uint8 mask = m_layers ? m_layers(textureKey, x, z) : 0;
		float weight = 0.0f;
		for (uint32 layer = 0; layer < 4; ++layer)
		{
			if ((mask & (1u << layer)) != 0)
			{
				weight += static_cast<float>((packed >> (layer * 8)) & 0xFFu) / 255.0f;
			}
		}
		return std::min(weight, 1.0f);
	}

	bool BaseTerrainView::WaterLevel(const float x, const float z, float& level) const
	{
		// Water quads and water vertices lie on the quad grid (quadsPerTile quads per tile side), which only matches the
		// outer vertex grid when quadsPerTile == verticesPerTile.
		const double quadSize = m_layout.quadsPerTile > 0
			? m_layout.vertexSpacing * m_layout.verticesPerTile / m_layout.quadsPerTile
			: 0.0;
		int64 vx = 0, vz = 0;
		float fx = 0.0f, fz = 0.0f;
		if ((m_waterQuads.empty() && m_waterHeights.empty()) || !GridCell(x, z, quadSize, vx, vz, fx, fz))
		{
			return m_live.WaterLevel(x, z, level);
		}

		// The quad under the position spans water vertices (vx, vz) .. (vx + 1, vz + 1).
		const auto quad = m_waterQuads.find(Pack(vx, vz));
		const bool heightsRecorded = m_waterHeights.count(Pack(vx, vz)) != 0 || m_waterHeights.count(Pack(vx + 1, vz)) != 0 ||
			m_waterHeights.count(Pack(vx, vz + 1)) != 0 || m_waterHeights.count(Pack(vx + 1, vz + 1)) != 0;
		if (quad == m_waterQuads.end() && !heightsRecorded)
		{
			return m_live.WaterLevel(x, z, level);
		}

		const uint8 live = m_access.GetWaterQuad(static_cast<int32>(vx), static_cast<int32>(vz));
		const uint8 type = quad != m_waterQuads.end() && live == quad->second.written ? quad->second.base : live;
		if (type == 0)
		{
			return false;
		}

		// Terrain::GetWaterHeightAtWorldPos: bilinear over the water vertices.
		const float h00 = BaseWaterHeight(vx, vz);
		const float h10 = BaseWaterHeight(vx + 1, vz);
		const float h01 = BaseWaterHeight(vx, vz + 1);
		const float h11 = BaseWaterHeight(vx + 1, vz + 1);
		level = h00 * (1.0f - fx) * (1.0f - fz) + h10 * fx * (1.0f - fz) + h01 * (1.0f - fx) * fz + h11 * fx * fz;
		return true;
	}

	uint64 BaseTerrainView::Revision() const
	{
		Hasher hasher;
		hasher.U64(m_live.Revision());
		hasher.U64(m_contributionHash);
		return hasher.Result();
	}
}
