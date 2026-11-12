// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "pcg/terrain_access.h"

#include <vector>

namespace
{
	/// In-memory terrain of tiles x tiles tiles of 8 vertex steps, 1 m spacing, so world 0..8*tiles on both axes.
	/// The default is 2x2 tiles (world 0..16).
	class FakeTerrain final : public mmo::pcg::ITerrainAccess
	{
	public:
		static constexpr mmo::int32 N = 17;   // outer vertices / splat pixels / water vertices per side of the default size
		static constexpr mmo::int32 Q = 16;   // inner vertices / water quads per side of the default size

		struct Data
		{
			std::vector<float> outer, inner, water;
			std::vector<mmo::uint32> splat;
			std::vector<mmo::uint8> quads;
			std::vector<mmo::String> materials;

			bool operator==(const Data& o) const
			{
				return outer == o.outer && inner == o.inner && water == o.water && splat == o.splat && quads == o.quads && materials == o.materials;
			}
		};

		explicit FakeTerrain(const mmo::int32 tiles = 2)
			: m_tiles(tiles)
			, m_n(tiles * 8 + 1)
			, m_q(tiles * 8)
		{
			m_layout.originX = 0.0;
			m_layout.originZ = 0.0;
			m_layout.vertexSpacing = 1.0;
			m_layout.pixelSpacing = 1.0;
			m_layout.quadsPerTile = 8;
			m_layout.verticesPerTile = 8;
			m_layout.tilesX = static_cast<mmo::uint32>(tiles);
			m_layout.tilesZ = static_cast<mmo::uint32>(tiles);
			m_data.outer.assign(m_n * m_n, 10.0f);
			m_data.inner.assign(m_q * m_q, 10.0f);
			m_data.water.assign(m_n * m_n, 0.0f);
			m_data.splat.assign(m_n * m_n, 0xFFu);
			m_data.quads.assign(m_q * m_q, 0);
			m_data.materials.assign(m_tiles * m_tiles, "Base");
		}

		const mmo::pcg::GridLayout& GetLayout() const override { return m_layout; }
		float GetOuterHeight(mmo::int32 x, mmo::int32 z) const override { return m_data.outer[z * m_n + x]; }
		void SetOuterHeight(mmo::int32 x, mmo::int32 z, float h) override { m_data.outer[z * m_n + x] = h; }
		float GetInnerHeight(mmo::int32 x, mmo::int32 z) const override { return m_data.inner[z * m_q + x]; }
		void SetInnerHeight(mmo::int32 x, mmo::int32 z, float h) override { m_data.inner[z * m_q + x] = h; }
		mmo::uint32 GetSplat(mmo::int32 x, mmo::int32 z) const override { return m_data.splat[z * m_n + x]; }
		void SetSplat(mmo::int32 x, mmo::int32 z, mmo::uint32 p) override { m_data.splat[z * m_n + x] = p; }
		mmo::uint8 GetWaterQuad(mmo::int32 x, mmo::int32 z) const override { return m_data.quads[z * m_q + x]; }
		void SetWaterQuad(mmo::int32 x, mmo::int32 z, mmo::uint8 t) override { m_data.quads[z * m_q + x] = t; }
		float GetWaterHeight(mmo::int32 x, mmo::int32 z) const override { return m_data.water[z * m_n + x]; }
		void SetWaterHeight(mmo::int32 x, mmo::int32 z, float h) override { m_data.water[z * m_n + x] = h; }
		mmo::String GetTileMaterial(mmo::int32 x, mmo::int32 z) const override { return m_data.materials[z * m_tiles + x]; }
		void SetTileMaterial(mmo::int32 x, mmo::int32 z, const mmo::String& m) override { m_data.materials[z * m_tiles + x] = m; }
		void FinishEdits(const mmo::pcg::IntRect&) override { ++m_finishCount; }

		Data m_data;
		int m_finishCount = 0;

	private:
		mmo::int32 m_tiles, m_n, m_q;
		mmo::pcg::GridLayout m_layout;
	};
}
