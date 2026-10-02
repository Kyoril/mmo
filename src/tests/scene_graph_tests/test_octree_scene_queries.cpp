// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

// math/ray.h uses int32 without pulling the typedefs in itself, so this has to come first.
#include "base/typedefs.h"

#include "math/aabb.h"
#include "math/ray.h"
#include "null_device.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/octree.h"
#include "scene_graph/octree_node.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/scene_node.h"

#include <algorithm>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	/// A movable object with fixed local bounds.
	class BoxObject final : public MovableObject
	{
	public:
		BoxObject(const String& name, const AABB& bounds)
			: MovableObject(name)
			, m_bounds(bounds)
		{
		}

		// ~ Begin MovableObject
		[[nodiscard]] const String& GetMovableType() const override { return m_type; }
		[[nodiscard]] const AABB& GetBoundingBox() const override { return m_bounds; }
		[[nodiscard]] float GetBoundingRadius() const override { return m_bounds.GetExtents().GetLength(); }
		void VisitRenderables(Renderable::Visitor&, bool) override {}
		void PopulateRenderQueue(RenderQueue&) override {}
		// ~ End MovableObject

	private:
		AABB m_bounds;
		String m_type { "BoxObject" };
	};

	bool Contains(const SceneQueryResult& result, const MovableObject& object)
	{
		return std::find(result.begin(), result.end(), &object) != result.end();
	}

	bool Contains(const RaySceneQueryResult& result, const MovableObject& object)
	{
		return std::any_of(result.begin(), result.end(), [&object](const RaySceneQueryResultEntry& entry)
		{
			return entry.movable == &object;
		});
	}
}

// The octree is loose: a node stays in its octant for as long as its centre is inside the octant
// and it is smaller than the octant (OctreeNode::IsInAABB), so it may hang out of the octant by up
// to half the octant's size. A node that grows after it was placed does exactly that - a terrain
// page attaches one tile per frame to the same scene node, so its box grows from one tile to the
// whole page and ends up in an octant chosen for a fraction of it.
//
// Rendering culls octants against their loosened bounds (Octree::GetCullBounds), so such a page
// is still drawn. The AABB and ray queries used to test the tight octant box instead, so they
// never saw the part of the page sticking out of it: the player's collision sweep found no
// terrain there and fell through the ground right at the page border.
TEST_CASE("OctreeScene AABB query finds a grown node outside its tight octant box", "[octree_scene]")
{
	EnsureNullDevice();

	// The first object is tiny, so the node is placed in a deep octant. At the default depth of 8
	// the octree cells are 34200 / 256 = 133.6 units wide and one of them starts at 0, so a node
	// around (10, 10, 10) lands in the cell [0, 133.6]^3.
	BoxObject first("First", AABB(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f)));

	// The second object grows the node to x in [-30, 50]: its centre (10) stays in the cell and
	// it is still smaller than the cell, so the node is not moved - but it now covers x < 0.
	BoxObject second("Second", AABB(Vector3(-40.0f, -1.0f, -1.0f), Vector3(40.0f, 1.0f, 1.0f)));

	OctreeScene scene;
	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode("Grown", Vector3(10.0f, 10.0f, 10.0f));
	node->AttachObject(first);
	scene.GetRootSceneNode().Update(true, false);

	const Octree* octant = static_cast<OctreeNode*>(node)->GetOctant();
	REQUIRE(octant != nullptr);
	REQUIRE(octant->m_box.min.x == Approx(0.0f).margin(0.01f));

	node->AttachObject(second);
	scene.GetRootSceneNode().Update(true, false);

	// Precondition of the scenario: the node stayed put and now hangs out of its octant.
	REQUIRE(static_cast<OctreeNode*>(node)->GetOctant() == octant);
	REQUIRE(second.GetWorldBoundingBox(true).min.x < octant->m_box.min.x);

	SECTION("AABB query in the overhanging part")
	{
		const auto query = scene.CreateAABBQuery(AABB(Vector3(-25.0f, 9.0f, 9.0f), Vector3(-20.0f, 11.0f, 11.0f)));
		query->Execute(*query);
		const SceneQueryResult& result = query->GetLastResult();

		CHECK(Contains(result, second));
		CHECK_FALSE(Contains(result, first));
	}

	SECTION("Ray query through the overhanging part")
	{
		// Straight down through x = -20, where only the overhang of the node is.
		const auto query = scene.CreateRayQuery(Ray(Vector3(-20.0f, 100.0f, 10.0f), Vector3(-20.0f, -100.0f, 10.0f)));
		const RaySceneQueryResult& result = query->Execute();

		CHECK(Contains(result, second));
		CHECK_FALSE(Contains(result, first));
	}
}

// The other half of the loose-bounds rule: nodes outside the octree are forced into the root
// octant, which must therefore never be culled by its own box.
TEST_CASE("OctreeScene AABB query finds nodes forced into the root octant", "[octree_scene]")
{
	EnsureNullDevice();

	BoxObject outside("Outside", AABB(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f)));

	OctreeScene scene;
	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode("Outside", Vector3(60000.0f, 0.0f, 0.0f));
	node->AttachObject(outside);
	scene.GetRootSceneNode().Update(true, false);
	REQUIRE(static_cast<OctreeNode*>(node)->GetOctant() != nullptr);

	const auto query = scene.CreateAABBQuery(AABB(Vector3(59990.0f, -10.0f, -10.0f), Vector3(60010.0f, 10.0f, 10.0f)));
	query->Execute(*query);
	CHECK(Contains(query->GetLastResult(), outside));
}
