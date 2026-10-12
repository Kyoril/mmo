// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/stair_ramp.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace mmo;

namespace
{
	struct Geometry
	{
		std::vector<Vector3> vertices;
		std::vector<uint32> indices;
		std::vector<uint16> faceSubMeshes;

		/// A quad a-b-c-d as two triangles, wound a-b-c / a-c-d.
		void Quad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const uint16 subMesh)
		{
			const uint32 base = static_cast<uint32>(vertices.size());
			vertices.insert(vertices.end(), { a, b, c, d });
			indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
			faceSubMeshes.insert(faceSubMeshes.end(), { subMesh, subMesh });
		}
	};

	constexpr float Rise = 0.33f;
	constexpr float Going = 0.5f;
	constexpr int Steps = 6;
	constexpr float Width = 2.0f;

	/// Six steps rising along +X (0.33 high, 0.5 deep), then a one unit deep landing at the top, two
	/// units wide along Z, with a base plate, risers, treads and side faces (submesh 1) - treads and
	/// risers are submesh 0. Quads are wound counter-clockwise seen from outside.
	Geometry Staircase()
	{
		Geometry g;
		const float landing = Steps * Going;
		const float top = (Steps + 1) * Rise;

		// Base plate, facing down
		g.Quad(Vector3(0, 0, 0), Vector3(landing + 1, 0, 0), Vector3(landing + 1, 0, Width), Vector3(0, 0, Width), 1);

		for (int k = 0; k <= Steps; ++k)
		{
			const float x0 = k * Going;
			const float x1 = k == Steps ? landing + 1 : x0 + Going;
			const float y0 = k * Rise;
			const float y1 = y0 + Rise;

			// Riser facing -X, tread facing up
			g.Quad(Vector3(x0, y0, Width), Vector3(x0, y1, Width), Vector3(x0, y1, 0), Vector3(x0, y0, 0), 0);
			g.Quad(Vector3(x0, y1, 0), Vector3(x0, y1, Width), Vector3(x1, y1, Width), Vector3(x1, y1, 0), 0);

			// Sides of this step's column, facing -Z and +Z
			g.Quad(Vector3(x0, 0, 0), Vector3(x0, y1, 0), Vector3(x1, y1, 0), Vector3(x1, 0, 0), 1);
			g.Quad(Vector3(x1, 0, Width), Vector3(x1, y1, Width), Vector3(x0, y1, Width), Vector3(x0, 0, Width), 1);
		}

		// Back wall of the landing, facing +X
		g.Quad(Vector3(landing + 1, 0, 0), Vector3(landing + 1, top, 0), Vector3(landing + 1, top, Width), Vector3(landing + 1, 0, Width), 1);
		(void)top;
		return g;
	}

	size_t CountFacesWhere(const StairRampResult& r, bool (*predicate)(const Vector3& a, const Vector3& b, const Vector3& c))
	{
		size_t count = 0;
		for (size_t f = 0; f + 2 < r.indices.size(); f += 3)
		{
			if (predicate(r.vertices[r.indices[f]], r.vertices[r.indices[f + 1]], r.vertices[r.indices[f + 2]]))
			{
				++count;
			}
		}
		return count;
	}
}

TEST_CASE("Stair steps become one ramp from the foot to the landing", "[stair_ramp]")
{
	const Geometry stairs = Staircase();
	const StairRampResult r = BuildStairRamp(stairs.vertices, stairs.indices, stairs.faceSubMeshes);
	REQUIRE(r.success);
	INFO(r.message);

	// The six treads and seven risers (the last one leads onto the landing), two faces each
	CHECK(r.removedFaces == (Steps * 2 + 1) * 2);
	CHECK(r.slopeDegrees == Approx(std::atan(7 * Rise / (Steps * Going)) * 57.2957795f).margin(0.5f));
	REQUIRE(r.faceSubMeshes.size() == r.indices.size() / 3);

	// No riser is left between the foot and the landing
	CHECK(CountFacesWhere(r, [](const Vector3& a, const Vector3& b, const Vector3& c)
	{
		const Vector3 n = (b - a).Cross(c - a);
		return std::abs(n.x) > 0.5f * n.GetLength() && std::max({ a.x, b.x, c.x }) < 2.99f;
	}) == 0);

	// The landing, the side faces, the base and the back wall stay
	CHECK(CountFacesWhere(r, [](const Vector3& a, const Vector3& b, const Vector3& c)
	{
		return a.y == Approx(7 * Rise) && b.y == Approx(7 * Rise) && c.y == Approx(7 * Rise);
	}) == 2);
	CHECK(CountFacesWhere(r, [](const Vector3& a, const Vector3& b, const Vector3& c)
	{
		const Vector3 n = (b - a).Cross(c - a);
		return std::abs(n.z) > 0.9f * n.GetLength();
	}) == (Steps + 1) * 4);

	// The ramp: last two faces, from the foot on the ground to the landing's front edge, facing up,
	// using the treads' submesh
	const size_t f = r.indices.size() - 6;
	const Vector3& foot = r.vertices[r.indices[f]];
	CHECK(foot.x == Approx(0.0f).margin(0.001f));
	CHECK(foot.y == Approx(0.0f).margin(0.001f));
	const Vector3& head = r.vertices[r.indices[f + 2]];
	CHECK(head.x == Approx(Steps * Going).margin(0.001f));
	CHECK(head.y == Approx(7 * Rise).margin(0.001f));
	const Vector3 rampNormal = (r.vertices[r.indices[f + 1]] - r.vertices[r.indices[f]]).Cross(r.vertices[r.indices[f + 2]] - r.vertices[r.indices[f]]);
	CHECK(rampNormal.y > 0.0f);
	CHECK(r.faceSubMeshes.back() == 0);
}

TEST_CASE("Stair ramp follows the stairs' orientation and winding", "[stair_ramp]")
{
	// The same stairs turned to rise along -Z, wound the other way round
	Geometry stairs = Staircase();
	for (Vector3& v : stairs.vertices)
	{
		v = Vector3(v.z, v.y, -v.x);
	}
	for (size_t f = 0; f < stairs.indices.size(); f += 3)
	{
		std::swap(stairs.indices[f + 1], stairs.indices[f + 2]);
	}

	const StairRampResult r = BuildStairRamp(stairs.vertices, stairs.indices, {});
	REQUIRE(r.success);
	CHECK(r.removedFaces == (Steps * 2 + 1) * 2);
	CHECK(r.faceSubMeshes.empty());

	const size_t f = r.indices.size() - 6;
	const Vector3 rampNormal = (r.vertices[r.indices[f + 1]] - r.vertices[r.indices[f]]).Cross(r.vertices[r.indices[f + 2]] - r.vertices[r.indices[f]]);
	CHECK(rampNormal.y < 0.0f);
	float lowestZ = 0.0f;
	for (size_t v = r.vertices.size() - 4; v < r.vertices.size(); ++v)
	{
		lowestZ = std::min(lowestZ, r.vertices[v].z);
	}
	CHECK(lowestZ == Approx(-Steps * Going).margin(0.001f));
}

TEST_CASE("Stair ramp refuses geometry that is not a straight staircase", "[stair_ramp]")
{
	CHECK_FALSE(BuildStairRamp({}, {}, {}).success);

	// A raised floor: flat, no incline
	Geometry floor;
	floor.Quad(Vector3(0, 0, 0), Vector3(0, 0, 2), Vector3(4, 0, 2), Vector3(4, 0, 0), 0);
	floor.Quad(Vector3(0, 1, 0), Vector3(0, 1, 2), Vector3(4, 1, 2), Vector3(4, 1, 0), 0);
	const StairRampResult r = BuildStairRamp(floor.vertices, floor.indices, floor.faceSubMeshes);
	CHECK_FALSE(r.success);
	CHECK_FALSE(r.message.empty());
}
