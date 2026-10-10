// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "math/collision_shape.h"
#include "math/aabb_tree.h"
#include "math/ray.h"

#include <cmath>
#include <map>
#include <tuple>

using namespace mmo;

namespace
{
	struct TriangleSoup
	{
		std::vector<Vector3> vertices;
		std::vector<uint32> indices;
	};

	TriangleSoup Tessellate(const CollisionShape& shape)
	{
		TriangleSoup soup;
		TessellateCollisionShape(shape, soup.vertices, soup.indices);
		return soup;
	}

	typedef std::tuple<long, long, long> PositionKey;

	PositionKey KeyOf(const Vector3& p)
	{
		return { std::lround(p.x * 1000.0f), std::lround(p.y * 1000.0f), std::lround(p.z * 1000.0f) };
	}

	/// Closed and consistently wound: every directed edge (by position) occurs exactly once and its
	/// reverse occurs exactly once.
	bool IsClosedAndConsistentlyWound(const TriangleSoup& soup)
	{
		std::map<std::pair<PositionKey, PositionKey>, int> directed;
		for (size_t f = 0; f + 2 < soup.indices.size(); f += 3)
		{
			for (int e = 0; e < 3; ++e)
			{
				const Vector3& a = soup.vertices[soup.indices[f + e]];
				const Vector3& b = soup.vertices[soup.indices[f + (e + 1) % 3]];
				++directed[{ KeyOf(a), KeyOf(b) }];
			}
		}

		for (const auto& [edge, count] : directed)
		{
			if (count != 1)
			{
				return false;
			}

			const auto reverse = directed.find({ edge.second, edge.first });
			if (reverse == directed.end() || reverse->second != 1)
			{
				return false;
			}
		}

		return !directed.empty();
	}

	/// For convex shapes: every face normal points away from a known interior point.
	bool AllFacesPointAwayFrom(const TriangleSoup& soup, const Vector3& interior)
	{
		for (size_t f = 0; f + 2 < soup.indices.size(); f += 3)
		{
			const Vector3& a = soup.vertices[soup.indices[f]];
			const Vector3& b = soup.vertices[soup.indices[f + 1]];
			const Vector3& c = soup.vertices[soup.indices[f + 2]];
			const Vector3 normal = (b - a).Cross(c - a);
			const Vector3 centroid = (a + b + c) / 3.0f;
			if (normal.Dot(centroid - interior) <= 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	AABB BoundsOf(const TriangleSoup& soup)
	{
		AABB box(soup.vertices.front(), soup.vertices.front());
		for (const auto& v : soup.vertices)
		{
			box.Combine(v);
		}
		return box;
	}

	CollisionShape MakeShape(collision_shape_type::Type type, const Vector3& position, const Vector3& scale)
	{
		CollisionShape shape;
		shape.type = type;
		shape.position = position;
		shape.scale = scale;
		return shape;
	}

	float HitHeight(const Ray& ray)
	{
		return (ray.origin + (ray.destination - ray.origin) * ray.hitDistance).y;
	}
}

TEST_CASE("Box shape is a closed outward box of the given size", "[collision_shape]")
{
	const CollisionShape box = MakeShape(collision_shape_type::Box, Vector3(1.0f, 2.0f, 3.0f), Vector3(2.0f, 4.0f, 6.0f));
	const TriangleSoup soup = Tessellate(box);

	CHECK(soup.indices.size() == 36);
	CHECK(IsClosedAndConsistentlyWound(soup));
	CHECK(AllFacesPointAwayFrom(soup, box.position));

	const AABB bounds = BoundsOf(soup);
	CHECK(bounds.min.x == Approx(0.0f).margin(1e-5));
	CHECK(bounds.max.x == Approx(2.0f));
	CHECK(bounds.min.y == Approx(0.0f).margin(1e-5));
	CHECK(bounds.max.y == Approx(4.0f));
	CHECK(bounds.min.z == Approx(0.0f).margin(1e-5));
	CHECK(bounds.max.z == Approx(6.0f));
}

TEST_CASE("Rotated box is still outward and front faces block an outside ray", "[collision_shape]")
{
	CollisionShape box = MakeShape(collision_shape_type::Box, Vector3::Zero, Vector3(2.0f, 2.0f, 2.0f));
	box.rotation = Quaternion(Degree(30.0f), Vector3::UnitY);
	const TriangleSoup soup = Tessellate(box);

	CHECK(IsClosedAndConsistentlyWound(soup));
	CHECK(AllFacesPointAwayFrom(soup, Vector3::Zero));

	AABBTree tree;
	tree.Build(soup.vertices, soup.indices);
	Ray ray(Vector3(0.0f, 10.0f, 0.0f), Vector3(0.0f, -10.0f, 0.0f));
	REQUIRE(tree.IntersectRay(ray, nullptr, raycast_flags::IgnoreBackface));
	CHECK(HitHeight(ray) == Approx(1.0f));
}

TEST_CASE("Wedge rises along local +Z and is closed", "[collision_shape]")
{
	const CollisionShape wedge = MakeShape(collision_shape_type::Wedge, Vector3::Zero, Vector3(2.0f, 1.0f, 4.0f));
	const TriangleSoup soup = Tessellate(wedge);

	CHECK(soup.indices.size() == 24);
	CHECK(IsClosedAndConsistentlyWound(soup));
	CHECK(AllFacesPointAwayFrom(soup, Vector3(0.0f, -0.25f, 1.0f)));

	AABBTree tree;
	tree.Build(soup.vertices, soup.indices);

	// Halfway up the ramp (z = 0) the top is at y = 0; three quarters up (z = 1) at y = 0.25.
	Ray mid(Vector3(0.1f, 5.0f, 0.0f), Vector3(0.1f, -5.0f, 0.0f));
	REQUIRE(tree.IntersectRay(mid, nullptr, raycast_flags::IgnoreBackface));
	CHECK(HitHeight(mid) == Approx(0.0f).margin(1e-4));

	Ray upper(Vector3(0.1f, 5.0f, 1.0f), Vector3(0.1f, -5.0f, 1.0f));
	REQUIRE(tree.IntersectRay(upper, nullptr, raycast_flags::IgnoreBackface));
	CHECK(HitHeight(upper) == Approx(0.25f).margin(1e-4));
}

TEST_CASE("Plane is one quad facing +Y, two-sided adds the reverse face", "[collision_shape]")
{
	CollisionShape plane = MakeShape(collision_shape_type::Plane, Vector3(0.0f, 1.0f, 0.0f), Vector3(4.0f, 1.0f, 4.0f));
	TriangleSoup soup = Tessellate(plane);
	REQUIRE(soup.indices.size() == 6);
	const Vector3 n = (soup.vertices[soup.indices[1]] - soup.vertices[soup.indices[0]]).Cross(soup.vertices[soup.indices[2]] - soup.vertices[soup.indices[0]]);
	CHECK(n.y > 0.0f);

	AABBTree oneSided;
	oneSided.Build(soup.vertices, soup.indices);
	Ray fromBelow(Vector3(0.1f, -5.0f, 0.2f), Vector3(0.1f, 5.0f, 0.2f));
	CHECK_FALSE(oneSided.IntersectRay(fromBelow, nullptr, raycast_flags::IgnoreBackface));

	plane.twoSided = true;
	soup = Tessellate(plane);
	CHECK(soup.indices.size() == 12);
	AABBTree twoSided;
	twoSided.Build(soup.vertices, soup.indices);
	Ray fromBelowAgain(Vector3(0.1f, -5.0f, 0.2f), Vector3(0.1f, 5.0f, 0.2f));
	CHECK(twoSided.IntersectRay(fromBelowAgain, nullptr, raycast_flags::IgnoreBackface));
}

TEST_CASE("Point inside box and wedge respects rotation and scale", "[collision_shape]")
{
	CollisionShape box = MakeShape(collision_shape_type::Box, Vector3(10.0f, 0.0f, 0.0f), Vector3(4.0f, 1.0f, 1.0f));
	box.rotation = Quaternion(Degree(90.0f), Vector3::UnitY);   // long axis now along Z
	CHECK(IsPointInsideCollisionShape(box, Vector3(10.0f, 0.0f, 1.5f)));
	CHECK_FALSE(IsPointInsideCollisionShape(box, Vector3(11.5f, 0.0f, 0.0f)));

	const CollisionShape wedge = MakeShape(collision_shape_type::Wedge, Vector3::Zero, Vector3(2.0f, 2.0f, 2.0f));
	CHECK(IsPointInsideCollisionShape(wedge, Vector3(0.0f, -0.5f, 0.5f)));         // under the slope
	CHECK_FALSE(IsPointInsideCollisionShape(wedge, Vector3(0.0f, 0.5f, -0.5f)));   // above the slope
}

TEST_CASE("Planes have no volume and do not support Cut", "[collision_shape]")
{
	const CollisionShape plane = MakeShape(collision_shape_type::Plane, Vector3::Zero, Vector3(4.0f, 4.0f, 4.0f));
	CHECK_FALSE(IsPointInsideCollisionShape(plane, Vector3::Zero));
	CHECK_FALSE(CollisionShapeSupportsCut(collision_shape_type::Plane));
	CHECK(CollisionShapeSupportsCut(collision_shape_type::Box));

	CollisionShape cutPlane = plane;
	cutPlane.op = collision_shape_op::Cut;
	CHECK(SanitizeCollisionShape(cutPlane).op == collision_shape_op::Add);
}

TEST_CASE("Sanitize clamps degenerate input", "[collision_shape]")
{
	CollisionShape shape = MakeShape(collision_shape_type::Box, Vector3::Zero, Vector3(0.0f, -2.0f, 1.0f));
	shape.segments = 0;
	const CollisionShape sane = SanitizeCollisionShape(shape);
	CHECK(sane.scale.x == Approx(0.001f));
	CHECK(sane.scale.y == Approx(2.0f));      // sign dropped: negative scale would invert the winding
	CHECK(sane.segments == 3);

	CHECK_FALSE(IsPointInsideCollisionShape(MakeShape(collision_shape_type::Box, Vector3::Zero, Vector3(0.0f, 1.0f, 1.0f)), Vector3(0.5f, 0.0f, 0.0f)));

	const TriangleSoup soup = Tessellate(MakeShape(collision_shape_type::Box, Vector3::Zero, Vector3(0.0f, 1.0f, 1.0f)));
	for (const auto& v : soup.vertices)
	{
		CHECK(std::isfinite(v.x));
	}
}

TEST_CASE("Walkability uses the absolute normal Y against 0.71", "[collision_shape]")
{
	// Flat, either winding: walkable.
	CHECK(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, 0, 1), Vector3(1, 0, 0)));
	CHECK(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)));
	// 40 degrees: walkable (cos 40 = 0.766). 50 degrees: too steep (cos 50 = 0.643).
	const float t40 = std::tan(40.0f * 3.14159265f / 180.0f);
	const float t50 = std::tan(50.0f * 3.14159265f / 180.0f);
	CHECK(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, t40, 1), Vector3(1, 0, 0)));
	CHECK_FALSE(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, t50, 1), Vector3(1, 0, 0)));
	// Wall and degenerate: not walkable.
	CHECK_FALSE(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0)));
	CHECK_FALSE(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, 0, 0), Vector3(1, 0, 0)));
}
