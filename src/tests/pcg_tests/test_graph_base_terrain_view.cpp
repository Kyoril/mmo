// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain.h"

#include "pcg/graph/base_terrain_view.h"
#include "pcg/terrain_contribution.h"

#include "math/math_utils.h"

#include <algorithm>
#include <cmath>

using namespace mmo;
using namespace mmo::pcg;
using namespace mmo::pcg::graph;

namespace
{
	/// The whole fake terrain is one page of 16 vertex steps.
	constexpr int32 kStepsPerPage = 16;

	/// "Layers" texture keys: "L<i>" matches layer i everywhere.
	uint8 LayerMask(const String& key, float, float)
	{
		return key.size() == 2 && key[0] == 'L' && key[1] >= '0' && key[1] <= '3' ? static_cast<uint8>(1u << (key[1] - '0')) : 0;
	}

	/// Live sampler reading a FakeTerrain the way the editor reads the engine terrain: bilinear outer heights, page
	/// normals (central differences, SNorm8, bilinear), truncated splat pixel, water quad + bilinear water heights.
	class GridSampler final : public ITerrainSampler
	{
	public:
		explicit GridSampler(const FakeTerrain& terrain)
			: m_terrain(terrain)
		{
		}

		bool Height(const float x, const float z, float& h) const override
		{
			if (x < 0.0f || z < 0.0f || x > 16.0f || z > 16.0f)
			{
				return false;
			}
			const int32 vx = std::min(15, static_cast<int32>(x));
			const int32 vz = std::min(15, static_cast<int32>(z));
			const float fx = x - vx;
			const float fz = z - vz;
			h = (1 - fx) * (1 - fz) * Outer(vx, vz) + fx * (1 - fz) * Outer(vx + 1, vz) + (1 - fx) * fz * Outer(vx, vz + 1) + fx * fz * Outer(vx + 1, vz + 1);
			return true;
		}

		Vector3 Normal(const float x, const float z) const override
		{
			const int32 vx = static_cast<int32>(x);
			const int32 vz = static_cast<int32>(z);
			const float fx = x - vx;
			const float fz = z - vz;
			const Vector3 n0 = VertexNormal(vx, vz) * (1.0f - fx) + VertexNormal(vx + 1, vz) * fx;
			const Vector3 n1 = VertexNormal(vx, vz + 1) * (1.0f - fx) + VertexNormal(vx + 1, vz + 1) * fx;
			return (n0 * (1.0f - fz) + n1 * fz).NormalizedCopy();
		}

		float SplatWeight(const String& key, const float x, const float z) const override
		{
			const uint32 packed = m_terrain.GetSplat(static_cast<int32>(x), static_cast<int32>(z));
			const uint8 mask = LayerMask(key, x, z);
			float w = 0.0f;
			for (uint32 l = 0; l < 4; ++l)
			{
				if ((mask & (1u << l)) != 0)
				{
					w += static_cast<float>((packed >> (l * 8)) & 0xFFu) / 255.0f;
				}
			}
			return std::min(w, 1.0f);
		}

		bool WaterLevel(const float x, const float z, float& level) const override
		{
			const int32 qx = static_cast<int32>(x);
			const int32 qz = static_cast<int32>(z);
			if (m_terrain.GetWaterQuad(qx, qz) == 0)
			{
				return false;
			}
			const float fx = x - qx;
			const float fz = z - qz;
			level = m_terrain.GetWaterHeight(qx, qz) * (1 - fx) * (1 - fz) + m_terrain.GetWaterHeight(qx + 1, qz) * fx * (1 - fz)
				+ m_terrain.GetWaterHeight(qx, qz + 1) * (1 - fx) * fz + m_terrain.GetWaterHeight(qx + 1, qz + 1) * fx * fz;
			return true;
		}

		uint64 Revision() const override
		{
			return revision;
		}

		uint64 revision = 1;

	private:
		float Outer(const int32 x, const int32 z) const
		{
			return m_terrain.GetOuterHeight(x, z);
		}

		Vector3 VertexNormal(const int32 x, const int32 z) const
		{
			const float scaling = 1.0f * kStepsPerPage / (kStepsPerPage + 1.0f);
			const float c = Outer(x, z);
			const float left = x > 0 ? Outer(x - 1, z) : c;
			const float up = z > 0 ? Outer(x, z - 1) : c;
			Vector3 n(-(Outer(x + 1, z) - left) / (2.0f * scaling), 1.0f, -(Outer(x, z + 1) - up) / (2.0f * scaling));
			n.Normalize();
			const EncodedNormal8 e = EncodeNormalSNorm8(n.x, n.y, n.z);
			Vector3 d;
			DecodeNormalSNorm8(e, d.x, d.y, d.z);
			return d;
		}

		const FakeTerrain& m_terrain;
	};

	/// Base terrain: a slope with a bump, layer 0 everywhere, no water.
	void MakeBase(FakeTerrain& terrain)
	{
		for (int32 z = 0; z < FakeTerrain::N; ++z)
		{
			for (int32 x = 0; x < FakeTerrain::N; ++x)
			{
				terrain.SetOuterHeight(x, z, 10.0f + 0.3f * x + 0.1f * z + (x == 6 && z == 6 ? 1.0f : 0.0f));
			}
		}
	}

	/// Raises a disc by 3, paints layer 1 inside, waters its centre at 14.
	TerrainTarget Pond(const float x, const float z, const float base)
	{
		TerrainTarget t;
		const float d = std::sqrt((x - 8.0f) * (x - 8.0f) + (z - 8.0f) * (z - 8.0f));
		if (d < 5.0f)
		{
			t.height = base + 3.0f;
			t.heightWeight = 1.0f;
			t.splat = { 0, 255, 0, 0 };
			t.splatWeight = 1.0f;
		}
		if (d < 3.0f)
		{
			t.waterType = 1;
			t.waterHeight = 14.0f;
		}
		return t;
	}

	const WorldRect kRect{ 2.0f, 2.0f, 14.0f, 14.0f };
}

TEST_CASE("BaseTerrainView reads the terrain as Unapply restores it", "[graph_base_terrain_view]")
{
	FakeTerrain live;
	MakeBase(live);
	TerrainContribution contribution;
	Apply(live, kRect, Pond, "Grass", contribution);
	REQUIRE(!contribution.outer.empty());
	REQUIRE(!contribution.splat.empty());
	REQUIRE(!contribution.waterQuads.empty());
	REQUIRE(!contribution.waterHeights.empty());

	// Hand edits after the build: one height raised, one painted pixel repainted. Unapply keeps both.
	live.SetOuterHeight(7, 8, live.GetOuterHeight(7, 8) + 2.0f);
	live.SetSplat(9, 8, 0x00FF0000u);

	FakeTerrain truth = live;
	TerrainContribution undo = contribution;
	Unapply(truth, undo);

	const GridSampler liveSampler(live);
	const GridSampler truthSampler(truth);
	const BaseTerrainView view(liveSampler, live, contribution, LayerMask, kStepsPerPage);

	for (float z = 1.25f; z < 14.9f; z += 0.7f)
	{
		for (float x = 1.1f; x < 14.9f; x += 0.65f)
		{
			float expected = 0.0f, actual = 0.0f;
			REQUIRE(truthSampler.Height(x, z, expected));
			REQUIRE(view.Height(x, z, actual));
			CHECK(actual == Approx(expected).margin(1e-4f));

			const Vector3 en = truthSampler.Normal(x, z);
			const Vector3 an = view.Normal(x, z);
			CHECK(an.x == Approx(en.x).margin(1e-5f));
			CHECK(an.y == Approx(en.y).margin(1e-5f));
			CHECK(an.z == Approx(en.z).margin(1e-5f));

			for (const char* key : { "L0", "L1", "L2", "none" })
			{
				CHECK(view.SplatWeight(key, x, z) == truthSampler.SplatWeight(key, x, z));
			}

			float expectedLevel = 0.0f, actualLevel = 0.0f;
			const bool expectedWater = truthSampler.WaterLevel(x, z, expectedLevel);
			CHECK(view.WaterLevel(x, z, actualLevel) == expectedWater);
			if (expectedWater)
			{
				CHECK(actualLevel == Approx(expectedLevel).margin(1e-5f));
			}
		}
	}

	// The live terrain really differs there (the view is not just passing it through).
	float liveHeight = 0.0f, viewHeight = 0.0f, level = 0.0f;
	REQUIRE(liveSampler.Height(8.0f, 8.0f, liveHeight));
	REQUIRE(view.Height(8.0f, 8.0f, viewHeight));
	CHECK(liveHeight - viewHeight == Approx(3.0f).margin(1e-4f));
	CHECK(liveSampler.WaterLevel(8.2f, 8.2f, level));
	CHECK_FALSE(view.WaterLevel(8.2f, 8.2f, level));
	CHECK(liveSampler.SplatWeight("L1", 8.0f, 8.0f) == 1.0f);
	CHECK(view.SplatWeight("L1", 8.0f, 8.0f) == 0.0f);
	CHECK(view.SplatWeight("L0", 8.0f, 8.0f) == 1.0f);
	// The repainted pixel keeps the hand paint.
	CHECK(view.SplatWeight("L2", 9.5f, 8.5f) == 1.0f);
	CHECK_FALSE(view.HasMaterialChanges());
}

TEST_CASE("BaseTerrainView height change is bilinear and 0 where nothing is recorded", "[graph_base_terrain_view]")
{
	FakeTerrain live;
	TerrainContribution contribution;
	contribution.outer.push_back({ 4, 4, 10.0f, 14.0f });
	contribution.outer.push_back({ 5, 4, 10.0f, 12.0f });
	const GridSampler liveSampler(live);
	const BaseTerrainView view(liveSampler, live, contribution, LayerMask, kStepsPerPage);

	CHECK(view.GetHeightChange(4.0f, 4.0f) == Approx(4.0f));
	CHECK(view.GetHeightChange(4.5f, 4.0f) == Approx(3.0f));
	// (4, 5) and (5, 5) are not recorded: they count 0.
	CHECK(view.GetHeightChange(4.5f, 4.5f) == Approx(1.5f));
	CHECK(view.GetHeightChange(10.0f, 10.0f) == 0.0f);

	// Far from every recorded sample the live answers pass through unchanged.
	float h = 0.0f;
	REQUIRE(view.Height(12.3f, 12.7f, h));
	float liveH = 0.0f;
	REQUIRE(liveSampler.Height(12.3f, 12.7f, liveH));
	CHECK(h == liveH);
	CHECK(view.Normal(12.3f, 12.7f).y == liveSampler.Normal(12.3f, 12.7f).y);
}

TEST_CASE("BaseTerrainView revision mixes the live revision and the contribution", "[graph_base_terrain_view]")
{
	FakeTerrain live;
	MakeBase(live);
	GridSampler liveSampler(live);
	TerrainContribution a;
	Apply(live, kRect, Pond, "Grass", a);
	TerrainContribution b = a;
	b.splat.front().written ^= 1u;
	TerrainContribution c = a;
	c.materials.push_back({ 0, 0, "Base", "Grass" });

	const BaseTerrainView viewA(liveSampler, live, a, LayerMask, kStepsPerPage);
	const BaseTerrainView viewA2(liveSampler, live, a, LayerMask, kStepsPerPage);
	const BaseTerrainView viewB(liveSampler, live, b, LayerMask, kStepsPerPage);
	const BaseTerrainView viewC(liveSampler, live, c, LayerMask, kStepsPerPage);
	CHECK(viewA.Revision() == viewA2.Revision());
	CHECK(viewA.Revision() != viewB.Revision());
	CHECK(viewA.Revision() != viewC.Revision());
	CHECK(viewC.HasMaterialChanges());

	const uint64 before = viewA.Revision();
	liveSampler.revision = 2;
	CHECK(viewA.Revision() != before);
}
