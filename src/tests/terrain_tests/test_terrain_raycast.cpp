// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "terrain/terrain_raycast.h"

#include <functional>
#include <vector>

using namespace mmo;
using namespace mmo::terrain::raycast;

namespace
{
	/// A synthetic outer-vertex height grid with independently settable inner vertices,
	/// mirroring how the real terrain stores its centre heights.
	class TestGrid
	{
	public:
		explicit TestGrid(const int32 cellsX, const int32 cellsZ, const float cellSize = 1.0f)
			: m_cellsX(cellsX)
			, m_cellsZ(cellsZ)
			, m_cellSize(cellSize)
			, m_heights(static_cast<size_t>(cellsX + 1) * static_cast<size_t>(cellsZ + 1), 0.0f)
			, m_inner(static_cast<size_t>(cellsX) * static_cast<size_t>(cellsZ), 0.0f)
			, m_innerSet(static_cast<size_t>(cellsX) * static_cast<size_t>(cellsZ), false)
		{
		}

		void SetHeight(const int32 vx, const int32 vz, const float height)
		{
			m_heights[static_cast<size_t>(vx) + static_cast<size_t>(vz) * (m_cellsX + 1)] = height;
		}

		float GetHeight(const int32 vx, const int32 vz) const
		{
			return m_heights[static_cast<size_t>(vx) + static_cast<size_t>(vz) * (m_cellsX + 1)];
		}

		/// Overrides a cell's inner vertex so it no longer equals the corner average.
		void SetInner(const int32 cx, const int32 cz, const float height)
		{
			const size_t index = static_cast<size_t>(cx) + static_cast<size_t>(cz) * m_cellsX;
			m_inner[index] = height;
			m_innerSet[index] = true;
		}

		/// Fills every outer vertex from a height function of the world position.
		void Fill(const std::function<float(float, float)>& fn)
		{
			for (int32 vz = 0; vz <= m_cellsZ; ++vz)
			{
				for (int32 vx = 0; vx <= m_cellsX; ++vx)
				{
					SetHeight(vx, vz, fn(static_cast<float>(vx) * m_cellSize, static_cast<float>(vz) * m_cellSize));
				}
			}
		}

		GridParams Params() const
		{
			GridParams params;
			params.cellSize = m_cellSize;
			params.originX = 0.0f;
			params.originZ = 0.0f;
			params.cellCountX = m_cellsX;
			params.cellCountZ = m_cellsZ;
			return params;
		}

		/// Marks a cell as not testable, standing in for a page that is not resident.
		void SetMissing(const int32 cx, const int32 cz)
		{
			m_missing.emplace_back(cx, cz);
		}

		bool Sample(const int32 cx, const int32 cz, CellHeights& out) const
		{
			for (const auto& [mx, mz] : m_missing)
			{
				if (mx == cx && mz == cz)
				{
					return false;
				}
			}

			out.corners[0] = GetHeight(cx, cz);
			out.corners[1] = GetHeight(cx + 1, cz);
			out.corners[2] = GetHeight(cx, cz + 1);
			out.corners[3] = GetHeight(cx + 1, cz + 1);

			const size_t index = static_cast<size_t>(cx) + static_cast<size_t>(cz) * m_cellsX;
			out.inner = m_innerSet[index]
				? m_inner[index]
				: (out.corners[0] + out.corners[1] + out.corners[2] + out.corners[3]) * 0.25f;

			return true;
		}

		Result Cast(const Ray& ray) const
		{
			return RaycastHeightGrid(ray, Params(), [this](const int32 cx, const int32 cz, CellHeights& out)
				{
					return Sample(cx, cz, out);
				});
		}

	private:
		int32 m_cellsX;
		int32 m_cellsZ;
		float m_cellSize;
		std::vector<float> m_heights;
		std::vector<float> m_inner;
		std::vector<bool> m_innerSet;
		std::vector<std::pair<int32, int32>> m_missing;
	};

	Ray MakeRay(const Vector3& origin, Vector3 direction, const float length = 1000.0f)
	{
		direction.Normalize();
		return Ray(origin, direction, length);
	}
}

TEST_CASE("Raycast_Flat_Grid_Straight_Down", "[terrain_raycast]")
{
	const TestGrid grid(8, 8);

	const Result result = grid.Cast(MakeRay(Vector3(2.5f, 10.0f, 3.5f), Vector3(0.0f, -1.0f, 0.0f)));

	REQUIRE(result.hit);
	CHECK(result.position.x == Approx(2.5f));
	CHECK(result.position.y == Approx(0.0f).margin(1e-4f));
	CHECK(result.position.z == Approx(3.5f));
	CHECK(result.distance == Approx(10.0f).margin(1e-3f));
	CHECK(result.cellX == 2);
	CHECK(result.cellZ == 3);
}

TEST_CASE("Raycast_Uses_Stored_Inner_Vertex_Height", "[terrain_raycast]")
{
	// The regression that made the brush miss "inner terrain vertices": inner vertices are
	// stored and deformed independently, so a raycast that averages the corners picks a
	// surface the renderer never draws.
	TestGrid grid(8, 8);
	grid.SetInner(2, 3, 5.0f);

	const Result result = grid.Cast(MakeRay(Vector3(2.5f, 10.0f, 3.5f), Vector3(0.0f, -1.0f, 0.0f)));

	REQUIRE(result.hit);
	CHECK(result.position.y == Approx(5.0f).margin(1e-3f));
	CHECK(result.cellX == 2);
	CHECK(result.cellZ == 3);
}

TEST_CASE("Raycast_Hits_Depressed_Inner_Vertex", "[terrain_raycast]")
{
	// The same mismatch in the other direction: a sculpted pit whose corners are untouched.
	TestGrid grid(8, 8);
	grid.SetInner(4, 4, -6.0f);

	const Result result = grid.Cast(MakeRay(Vector3(4.5f, 10.0f, 4.5f), Vector3(0.0f, -1.0f, 0.0f)));

	REQUIRE(result.hit);
	CHECK(result.position.y == Approx(-6.0f).margin(1e-3f));
}

TEST_CASE("Raycast_Subcell_Precision_On_Ramp", "[terrain_raycast]")
{
	// A linear ramp is exactly planar under the four-triangle fan, so the hit height must
	// match the analytic plane y = x rather than snapping to a vertex.
	TestGrid grid(16, 16);
	grid.Fill([](const float x, float) { return x; });

	for (const float sampleX : { 1.13f, 4.87f, 9.5f, 13.26f })
	{
		const Result result = grid.Cast(MakeRay(Vector3(sampleX, 40.0f, 7.31f), Vector3(0.0f, -1.0f, 0.0f)));

		REQUIRE(result.hit);
		CHECK(result.position.x == Approx(sampleX));
		CHECK(result.position.y == Approx(sampleX).margin(1e-3f));
	}
}

TEST_CASE("Raycast_Hits_Ridge_A_Coarse_Proxy_Would_Miss", "[terrain_raycast]")
{
	// The old implementation approximated a 4x4 cell block by its four corner heights. A
	// ridge that sits strictly between those corners vanished from the proxy surface, so a
	// near-horizontal ray passed straight through it.
	TestGrid grid(16, 16);
	grid.SetHeight(5, 8, 10.0f);
	grid.SetHeight(5, 9, 10.0f);
	grid.SetHeight(6, 8, 10.0f);
	grid.SetHeight(6, 9, 10.0f);

	const Result result = grid.Cast(MakeRay(Vector3(0.5f, 4.0f, 8.5f), Vector3(1.0f, 0.0f, 0.0f)));

	REQUIRE(result.hit);
	// The west flank of the ridge rises between x = 4 and x = 5.
	CHECK(result.position.x > 4.0f);
	CHECK(result.position.x < 5.0f);
}

TEST_CASE("Raycast_Returns_Nearest_Surface_Front_To_Back", "[terrain_raycast]")
{
	// Two ridges along the ray: the near one must win.
	TestGrid grid(16, 16);
	for (int32 vz = 0; vz <= 16; ++vz)
	{
		grid.SetHeight(4, vz, 10.0f);
		grid.SetHeight(5, vz, 10.0f);
		grid.SetHeight(11, vz, 20.0f);
		grid.SetHeight(12, vz, 20.0f);
	}

	const Result result = grid.Cast(MakeRay(Vector3(0.5f, 6.0f, 8.5f), Vector3(1.0f, 0.0f, 0.0f)));

	REQUIRE(result.hit);
	CHECK(result.position.x < 5.0f);
	CHECK(result.cellX == 3);
}

TEST_CASE("Raycast_Hits_Final_Cell_Strip", "[terrain_raycast]")
{
	// The old coarse pass skipped the trailing cells of every page outright, leaving a band
	// of terrain that could never be picked.
	const TestGrid grid(8, 8);

	const Result lastCell = grid.Cast(MakeRay(Vector3(7.5f, 10.0f, 7.5f), Vector3(0.0f, -1.0f, 0.0f)));
	REQUIRE(lastCell.hit);
	CHECK(lastCell.cellX == 7);
	CHECK(lastCell.cellZ == 7);

	const Result firstCell = grid.Cast(MakeRay(Vector3(0.25f, 10.0f, 0.25f), Vector3(0.0f, -1.0f, 0.0f)));
	REQUIRE(firstCell.hit);
	CHECK(firstCell.cellX == 0);
	CHECK(firstCell.cellZ == 0);
}

TEST_CASE("Raycast_Grazing_Ray_From_Far_Away", "[terrain_raycast]")
{
	// The zoomed-out case: a shallow ray travelling a long way before it reaches the target.
	// Cell size matches the real terrain lattice (PageSize / 128).
	TestGrid grid(128, 128, 4.1667f);
	grid.Fill([](const float x, const float z) { return 3.0f * std::sin(x * 0.01f) + 2.0f * std::cos(z * 0.013f); });

	// Roughly 5 degrees above the horizon, starting well outside the grid.
	const Vector3 origin(-200.0f, 60.0f, 260.0f);
	const Vector3 target(260.0f, 0.0f, 260.0f);
	const Result result = grid.Cast(MakeRay(origin, target - origin, 2000.0f));

	REQUIRE(result.hit);

	// The hit must lie on the surface: re-sample the cell it reported and compare.
	CellHeights heights;
	REQUIRE(grid.Sample(result.cellX, result.cellZ, heights));

	float low = heights.inner;
	float high = heights.inner;
	for (const float corner : heights.corners)
	{
		low = std::min(low, corner);
		high = std::max(high, corner);
	}

	CHECK(result.position.y >= Approx(low).margin(1e-3f));
	CHECK(result.position.y <= Approx(high).margin(1e-3f));

	// And the hit must actually be inside the cell the walk reported.
	const float cellMinX = static_cast<float>(result.cellX) * 4.1667f;
	const float cellMinZ = static_cast<float>(result.cellZ) * 4.1667f;
	CHECK(result.position.x >= Approx(cellMinX).margin(1e-2f));
	CHECK(result.position.x <= Approx(cellMinX + 4.1667f).margin(1e-2f));
	CHECK(result.position.z >= Approx(cellMinZ).margin(1e-2f));
	CHECK(result.position.z <= Approx(cellMinZ + 4.1667f).margin(1e-2f));
}

TEST_CASE("Raycast_Passes_Through_Non_Resident_Cells", "[terrain_raycast]")
{
	// Cells whose page is not resident must be traversed, not treated as a wall.
	TestGrid grid(16, 16);
	for (int32 vz = 0; vz <= 16; ++vz)
	{
		grid.SetHeight(9, vz, 12.0f);
		grid.SetHeight(10, vz, 12.0f);
	}

	for (int32 cx = 2; cx <= 6; ++cx)
	{
		grid.SetMissing(cx, 8);
	}

	const Result result = grid.Cast(MakeRay(Vector3(0.5f, 6.0f, 8.5f), Vector3(1.0f, 0.0f, 0.0f)));

	REQUIRE(result.hit);
	CHECK(result.cellX == 8);
}

TEST_CASE("Raycast_Respects_Ray_Length", "[terrain_raycast]")
{
	const TestGrid grid(16, 16);

	const Result tooShort = grid.Cast(MakeRay(Vector3(4.5f, 10.0f, 4.5f), Vector3(0.0f, -1.0f, 0.0f), 5.0f));
	CHECK_FALSE(tooShort.hit);

	const Result longEnough = grid.Cast(MakeRay(Vector3(4.5f, 10.0f, 4.5f), Vector3(0.0f, -1.0f, 0.0f), 20.0f));
	CHECK(longEnough.hit);
}

TEST_CASE("Raycast_Misses_When_Ray_Cannot_Reach_Terrain", "[terrain_raycast]")
{
	const TestGrid grid(16, 16);

	SECTION("Pointing away from the surface")
	{
		const Result result = grid.Cast(MakeRay(Vector3(4.5f, 10.0f, 4.5f), Vector3(0.0f, 1.0f, 0.0f)));
		CHECK_FALSE(result.hit);
	}

	SECTION("Travelling horizontally above the surface")
	{
		const Result result = grid.Cast(MakeRay(Vector3(0.5f, 25.0f, 4.5f), Vector3(1.0f, 0.0f, 0.0f)));
		CHECK_FALSE(result.hit);
	}

	SECTION("Never entering the grid")
	{
		const Result result = grid.Cast(MakeRay(Vector3(-50.0f, 10.0f, -50.0f), Vector3(-1.0f, -1.0f, 0.0f)));
		CHECK_FALSE(result.hit);
	}
}

TEST_CASE("Raycast_Handles_Empty_Grid", "[terrain_raycast]")
{
	GridParams params;
	params.cellSize = 1.0f;
	params.cellCountX = 0;
	params.cellCountZ = 0;

	const Result result = RaycastHeightGrid(MakeRay(Vector3(0.0f, 10.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f)), params,
		[](int32, int32, CellHeights&) { return true; });

	CHECK_FALSE(result.hit);
}

TEST_CASE("Raycast_Samples_Each_Crossed_Cell_At_Most_Once", "[terrain_raycast]")
{
	// A diagonal walk across the whole grid must stay proportional to the cells crossed.
	GridParams params;
	params.cellSize = 1.0f;
	params.cellCountX = 64;
	params.cellCountZ = 64;

	int32 sampleCount = 0;
	const Vector3 origin(0.5f, 5.0f, 0.5f);
	const Vector3 direction(1.0f, 0.0f, 1.0f);

	const Result result = RaycastHeightGrid(MakeRay(origin, direction, 500.0f), params,
		[&sampleCount](int32, int32, CellHeights&)
		{
			++sampleCount;
			return true;
		});

	CHECK_FALSE(result.hit);
	CHECK(sampleCount > 0);
	CHECK(sampleCount <= 2 * (params.cellCountX + params.cellCountZ));
}
