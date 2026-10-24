// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/terrain_contribution.h"

#include <cmath>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	/// 2x2 tiles of 8 vertex steps, 1 m spacing, so world 0..16 on both axes.
	class FakeTerrain final : public ITerrainAccess
	{
	public:
		static constexpr int32 N = 17;   // outer vertices / splat pixels / water vertices per side
		static constexpr int32 Q = 16;   // inner vertices / water quads per side

		struct Data
		{
			std::vector<float> outer, inner, water;
			std::vector<uint32> splat;
			std::vector<uint8> quads;
			std::vector<String> materials;

			bool operator==(const Data& o) const
			{
				return outer == o.outer && inner == o.inner && water == o.water && splat == o.splat && quads == o.quads && materials == o.materials;
			}
		};

		FakeTerrain()
		{
			m_layout.originX = 0.0;
			m_layout.originZ = 0.0;
			m_layout.vertexSpacing = 1.0;
			m_layout.pixelSpacing = 1.0;
			m_layout.quadsPerTile = 8;
			m_layout.verticesPerTile = 8;
			m_data.outer.assign(N * N, 10.0f);
			m_data.inner.assign(Q * Q, 10.0f);
			m_data.water.assign(N * N, 0.0f);
			m_data.splat.assign(N * N, 0xFFu);
			m_data.quads.assign(Q * Q, 0);
			m_data.materials.assign(4, "Base");
		}

		const GridLayout& GetLayout() const override { return m_layout; }
		float GetOuterHeight(int32 x, int32 z) const override { return m_data.outer[z * N + x]; }
		void SetOuterHeight(int32 x, int32 z, float h) override { m_data.outer[z * N + x] = h; }
		float GetInnerHeight(int32 x, int32 z) const override { return m_data.inner[z * Q + x]; }
		void SetInnerHeight(int32 x, int32 z, float h) override { m_data.inner[z * Q + x] = h; }
		uint32 GetSplat(int32 x, int32 z) const override { return m_data.splat[z * N + x]; }
		void SetSplat(int32 x, int32 z, uint32 p) override { m_data.splat[z * N + x] = p; }
		uint8 GetWaterQuad(int32 x, int32 z) const override { return m_data.quads[z * Q + x]; }
		void SetWaterQuad(int32 x, int32 z, uint8 t) override { m_data.quads[z * Q + x] = t; }
		float GetWaterHeight(int32 x, int32 z) const override { return m_data.water[z * N + x]; }
		void SetWaterHeight(int32 x, int32 z, float h) override { m_data.water[z * N + x] = h; }
		String GetTileMaterial(int32 x, int32 z) const override { return m_data.materials[z * 2 + x]; }
		void SetTileMaterial(int32 x, int32 z, const String& m) override { m_data.materials[z * 2 + x] = m; }
		void FinishEdits(const IntRect&) override { ++m_finishCount; }

		Data m_data;
		int m_finishCount = 0;

	private:
		GridLayout m_layout;
	};

	/// Bowl of radius 10 around (8,8) with grass splat, a pond of radius 3 and a tile material.
	TerrainTarget Bowl(float x, float z, float)
	{
		const float d = std::sqrt((x - 8.0f) * (x - 8.0f) + (z - 8.0f) * (z - 8.0f));
		TerrainTarget t;
		if (d < 10.0f)
		{
			t.height = 5.0f;
			t.heightWeight = 1.0f;
			t.splat = { 0, 255, 0, 0 };
			t.splatWeight = 1.0f;
			t.setMaterial = true;
		}
		if (d < 3.0f)
		{
			t.waterType = 1;
			t.waterHeight = 6.0f;
		}
		return t;
	}

	const WorldRect FullRect{ 0.0f, 0.0f, 16.0f, 16.0f };
}

TEST_CASE("Apply then Unapply restores exactly", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;

	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);
	CHECK_FALSE(c.IsEmpty());
	CHECK_FALSE(terrain.m_data == original);
	CHECK(!c.outer.empty());
	CHECK(!c.inner.empty());
	CHECK(!c.splat.empty());
	CHECK(!c.waterQuads.empty());
	CHECK(!c.waterHeights.empty());
	CHECK(!c.materials.empty());

	Unapply(terrain, c);
	CHECK(terrain.m_data == original);
	CHECK(c.IsEmpty());
}

TEST_CASE("Rebuild twice is identical", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;

	Apply(terrain, FullRect, Bowl, "Grass", c);
	const FakeTerrain::Data first = terrain.m_data;
	const TerrainContribution firstC = c;

	Unapply(terrain, c);
	Apply(terrain, FullRect, Bowl, "Grass", c);

	CHECK(terrain.m_data == first);
	REQUIRE(c.outer.size() == firstC.outer.size());
	for (size_t i = 0; i < c.outer.size(); ++i)
	{
		CHECK(c.outer[i].x == firstC.outer[i].x);
		CHECK(c.outer[i].z == firstC.outer[i].z);
		CHECK(c.outer[i].base == firstC.outer[i].base);
		CHECK(c.outer[i].written == firstC.outer[i].written);
	}
	CHECK(c.inner.size() == firstC.inner.size());
	CHECK(c.splat.size() == firstC.splat.size());
	CHECK(c.waterQuads.size() == firstC.waterQuads.size());
	CHECK(c.waterHeights.size() == firstC.waterHeights.size());
	CHECK(c.materials.size() == firstC.materials.size());
}

TEST_CASE("Entries are sorted by z then x", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	auto sorted = [](const auto& list)
	{
		for (size_t i = 1; i < list.size(); ++i)
		{
			const auto& a = list[i - 1];
			const auto& b = list[i];
			if (a.z > b.z || (a.z == b.z && a.x >= b.x))
			{
				return false;
			}
		}
		return true;
	};
	CHECK(sorted(c.outer));
	CHECK(sorted(c.inner));
	CHECK(sorted(c.splat));
	CHECK(sorted(c.waterQuads));
	CHECK(sorted(c.waterHeights));
	CHECK(sorted(c.materials));
}

TEST_CASE("Hand sculpt under the result is kept", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	REQUIRE(!c.outer.empty());
	const auto e = c.outer[c.outer.size() / 2];
	terrain.SetOuterHeight(e.x, e.z, terrain.GetOuterHeight(e.x, e.z) + 2.0f);

	Unapply(terrain, c);

	FakeTerrain::Data expected = original;
	expected.outer[e.z * FakeTerrain::N + e.x] += 2.0f;
	CHECK(terrain.m_data == expected);
}

TEST_CASE("Hand paint is kept as new base", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	REQUIRE(!c.splat.empty());
	const auto e = c.splat.front();
	const uint32 handPainted = 0x01020304u;
	terrain.SetSplat(e.x, e.z, handPainted);

	Unapply(terrain, c);

	FakeTerrain::Data expected = original;
	expected.splat[e.z * FakeTerrain::N + e.x] = handPainted;
	CHECK(terrain.m_data == expected);
}

TEST_CASE("Splat normalisation sums to 255", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	const auto blend = [](float, float, float)
	{
		TerrainTarget t;
		t.splat = { 33, 33, 34, 0 };
		t.splatWeight = 0.5f;
		return t;
	};
	Apply(terrain, WorldRect{ 0.0f, 0.0f, 16.0f, 16.0f }, blend, "", c);

	REQUIRE(!c.splat.empty());
	for (const auto& e : c.splat)
	{
		const uint32 v = e.written;
		const uint32 sum = (v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF) + ((v >> 24) & 0xFF);
		CHECK(sum == 255);
	}
}

TEST_CASE("Outside the rect nothing changes", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data before = terrain.m_data;
	TerrainContribution c;
	Apply(terrain, WorldRect{ 4.0f, 4.0f, 12.0f, 12.0f }, Bowl, "Grass", c);
	REQUIRE(!c.outer.empty());

	const auto outside = [](int32 v) { return v < 4 || v > 12; };
	for (int32 z = 0; z < FakeTerrain::N; ++z)
	{
		for (int32 x = 0; x < FakeTerrain::N; ++x)
		{
			if (outside(x) || outside(z))
			{
				const size_t i = z * FakeTerrain::N + x;
				CHECK(terrain.m_data.outer[i] == before.outer[i]);
				CHECK(terrain.m_data.splat[i] == before.splat[i]);
				CHECK(terrain.m_data.water[i] == before.water[i]);
			}
		}
	}
	for (int32 z = 0; z < FakeTerrain::Q; ++z)
	{
		for (int32 x = 0; x < FakeTerrain::Q; ++x)
		{
			if (z < 4 || z > 11 || x < 4 || x > 11)
			{
				const size_t i = z * FakeTerrain::Q + x;
				CHECK(terrain.m_data.inner[i] == before.inner[i]);
			}
		}
	}
}

TEST_CASE("RectsOverlap", "[terrain_contribution]")
{
	CHECK(RectsOverlap({ 0, 0, 10, 10 }, { 5, 5, 15, 15 }));
	CHECK(RectsOverlap({ 0, 0, 10, 10 }, { 10, 10, 20, 20 }));
	CHECK(RectsOverlap({ 0, 0, 10, 10 }, { 2, 2, 3, 3 }));
	CHECK_FALSE(RectsOverlap({ 0, 0, 10, 10 }, { 11, 0, 20, 10 }));
	CHECK_FALSE(RectsOverlap({ 0, 0, 10, 10 }, { 0, -20, 10, -1 }));
}

TEST_CASE("Match ratio is 1 when the terrain carries the contribution", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	CHECK(ContributionMatchRatio(terrain, c) == 1.0f);

	Apply(terrain, FullRect, Bowl, "Grass", c);
	size_t matching = 0, total = 0;
	CHECK(ContributionMatchRatio(terrain, c, &matching, &total) == 1.0f);
	CHECK(total == c.outer.size() + c.inner.size());
	CHECK(matching == total);
	CHECK(total > 0);
}

TEST_CASE("Match ratio is 0 when the terrain was reset to the base", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	terrain.m_data = original;
	CHECK(ContributionMatchRatio(terrain, c) == 0.0f);
}

TEST_CASE("Match ratio is in between for partly sculpted terrain", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);
	REQUIRE(c.outer.size() >= 4);

	const size_t half = c.outer.size() / 2;
	for (size_t i = 0; i < half; ++i)
	{
		terrain.SetOuterHeight(c.outer[i].x, c.outer[i].z, c.outer[i].written + 1.0f);
	}

	size_t matching = 0, total = 0;
	const float ratio = ContributionMatchRatio(terrain, c, &matching, &total);
	CHECK(ratio > 0.0f);
	CHECK(ratio < 1.0f);
	CHECK(matching == total - half);
}
