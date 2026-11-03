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
		/// @param stepsPerPage Outer vertex steps per page: page-local index 0 has no left / up neighbour in the normal.
		explicit GridSampler(const FakeTerrain& terrain, const int32 stepsPerPage = kStepsPerPage)
			: m_terrain(terrain)
			, m_stepsPerPage(stepsPerPage)
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
			// Page::GetSmoothNormalAt: the four corner normals as the page holding the cell stores them (a vertex on a
			// page seam has one normal per page).
			const int32 px = vx - vx % m_stepsPerPage;
			const int32 pz = vz - vz % m_stepsPerPage;
			const Vector3 n0 = VertexNormal(vx, vz, px, pz) * (1.0f - fx) + VertexNormal(vx + 1, vz, px, pz) * fx;
			const Vector3 n1 = VertexNormal(vx, vz + 1, px, pz) * (1.0f - fx) + VertexNormal(vx + 1, vz + 1, px, pz) * fx;
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

		/// Page::CalculateNormalAt of the page whose first vertex is (pageX, pageZ): no left / up neighbour at its
		/// local index 0.
		Vector3 VertexNormal(const int32 x, const int32 z, const int32 pageX, const int32 pageZ) const
		{
			const float scaling = 1.0f * m_stepsPerPage / (m_stepsPerPage + 1.0f);
			const float c = Outer(x, z);
			const float left = x > pageX ? Outer(x - 1, z) : c;
			const float up = z > pageZ ? Outer(x, z - 1) : c;
			Vector3 n(-(Outer(x + 1, z) - left) / (2.0f * scaling), 1.0f, -(Outer(x, z + 1) - up) / (2.0f * scaling));
			n.Normalize();
			const EncodedNormal8 e = EncodeNormalSNorm8(n.x, n.y, n.z);
			Vector3 d;
			DecodeNormalSNorm8(e, d.x, d.y, d.z);
			return d;
		}

		const FakeTerrain& m_terrain;
		int32 m_stepsPerPage;
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

TEST_CASE("BaseTerrainView normals follow the page seam rule", "[graph_base_terrain_view]")
{
	// Two pages of 8 steps per axis: vertex 8 is page-local index 0 of the second page (no left / up neighbour).
	constexpr int32 stepsPerPage = 8;
	FakeTerrain live;
	MakeBase(live);
	TerrainContribution contribution;
	Apply(live, kRect, Pond, "Grass", contribution);
	REQUIRE(!contribution.outer.empty());

	FakeTerrain truth = live;
	TerrainContribution undo = contribution;
	Unapply(truth, undo);

	const GridSampler liveSampler(live, stepsPerPage);
	const GridSampler truthSampler(truth, stepsPerPage);
	const BaseTerrainView view(liveSampler, live, contribution, LayerMask, stepsPerPage);

	// Around and on the seam, including exactly on vertex 8 and at cell fractions that weight it fully.
	for (const float z : { 6.5f, 7.75f, 8.0f, 8.25f, 9.5f })
	{
		for (const float x : { 6.5f, 7.0f, 7.75f, 8.0f, 8.25f, 9.0f, 9.5f })
		{
			const Vector3 expected = truthSampler.Normal(x, z);
			const Vector3 actual = view.Normal(x, z);
			CHECK(actual.x == Approx(expected.x).margin(1e-5f));
			CHECK(actual.y == Approx(expected.y).margin(1e-5f));
			CHECK(actual.z == Approx(expected.z).margin(1e-5f));
		}
	}

	// The seam rule matters here: a one-page view (left neighbour at vertex 8) gives another normal on the seam, and
	// vertex 8 has a different normal in the cell left of it (page 0, which has the neighbour) than on it (page 1).
	const BaseTerrainView onePage(liveSampler, live, contribution, LayerMask, kStepsPerPage);
	const Vector3 seam = view.Normal(8.0f, 6.5f);
	const Vector3 noSeam = onePage.Normal(8.0f, 6.5f);
	CHECK(std::abs(seam.x - noSeam.x) > 1e-3f);
	const Vector3 leftOfSeam = view.Normal(7.9999f, 6.5f);
	CHECK(std::abs(seam.x - leftOfSeam.x) > 1e-3f);
}

TEST_CASE("BaseTerrainView keeps unrecorded water quads at their live type", "[graph_base_terrain_view]")
{
	// The engine stores one water type per tile: a build that waters quad (2, 2) changes the type of tile (0, 0), and
	// quad (5, 5) of that tile, which it did not record, now reads the new type live too. The view only undoes recorded
	// quads, so (5, 5) reads its live type, not the type it had before the build.
	FakeTerrain live;
	live.SetWaterQuad(2, 2, 1);
	live.SetWaterQuad(5, 5, 1);
	for (const int32 v : { 2, 3, 5, 6 })
	{
		live.SetWaterHeight(v, 2, 4.0f);
		live.SetWaterHeight(v, 3, 4.0f);
		live.SetWaterHeight(v, 5, 4.0f);
		live.SetWaterHeight(v, 6, 4.0f);
	}
	TerrainContribution contribution;
	contribution.waterQuads.push_back({ 2, 2, 0, 1 });

	const GridSampler liveSampler(live);
	const BaseTerrainView view(liveSampler, live, contribution, LayerMask, kStepsPerPage);
	float level = 0.0f;
	CHECK_FALSE(view.WaterLevel(2.5f, 2.5f, level));
	REQUIRE(view.WaterLevel(5.5f, 5.5f, level));
	CHECK(level == Approx(4.0f));
}

TEST_CASE("BaseTerrainView finds water quads on the quad grid", "[graph_base_terrain_view]")
{
	// A layout with 4 quads per tile of 8 vertex steps: quads are 2 m, water vertices 2 m apart.
	class CoarseWater final : public ITerrainAccess
	{
	public:
		CoarseWater()
		{
			m_layout.quadsPerTile = 4;
			m_layout.verticesPerTile = 8;
		}
		const GridLayout& GetLayout() const override { return m_layout; }
		float GetOuterHeight(int32, int32) const override { return 0.0f; }
		void SetOuterHeight(int32, int32, float) override {}
		float GetInnerHeight(int32, int32) const override { return 0.0f; }
		void SetInnerHeight(int32, int32, float) override {}
		uint32 GetSplat(int32, int32) const override { return 0xFFu; }
		void SetSplat(int32, int32, uint32) override {}
		uint8 GetWaterQuad(const int32 x, const int32 z) const override { return x == 1 && z == 1 ? 1 : 0; }
		void SetWaterQuad(int32, int32, uint8) override {}
		float GetWaterHeight(const int32 x, int32) const override { return x == 2 ? 6.0f : 4.0f; }
		void SetWaterHeight(int32, int32, float) override {}
		String GetTileMaterial(int32, int32) const override { return "Base"; }
		void SetTileMaterial(int32, int32, const String&) override {}
		void FinishEdits(const IntRect&) override {}

	private:
		GridLayout m_layout;
	};

	// The build watered quad (1, 1) (world 2..4) and its vertices (1..2, 1..2); before, (2, *) was at 5.
	const CoarseWater live;
	TerrainContribution contribution;
	contribution.waterQuads.push_back({ 1, 1, 2, 1 });
	contribution.waterHeights.push_back({ 2, 1, 5.0f, 6.0f });
	contribution.waterHeights.push_back({ 2, 2, 5.0f, 6.0f });
	FakeTerrain unused;
	const GridSampler liveSampler(unused);
	const BaseTerrainView view(liveSampler, live, contribution, LayerMask, kStepsPerPage);

	// Quad (1, 1) at world (3, 3), half way between water vertices 1 (4.0) and 2 (base 5.0): the base type 2.
	float level = 0.0f;
	REQUIRE(view.WaterLevel(3.0f, 3.0f, level));
	CHECK(level == Approx(4.5f));
	REQUIRE(view.WaterLevel(3.5f, 2.5f, level));
	CHECK(level == Approx(4.75f));
}
