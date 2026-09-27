// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/aabb.h"
#include "null_device.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/octree_node.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/scene_node.h"

#include <memory>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	/// A movable object with fixed bounds, so the node it is attached to gets real world bounds
	/// and is therefore inserted into the octree on its next update.
	class BoxObject final : public MovableObject
	{
	public:
		explicit BoxObject(const String& name)
			: MovableObject(name)
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
		AABB m_bounds { Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f) };
		String m_type { "BoxObject" };
	};
}

// The base Scene destructor owns the scene nodes, but an OctreeNode unregisters itself from its
// scene's octree when destroyed. Base-class members die after derived ones, so if OctreeScene let
// Scene tear the nodes down, every node still registered in an octant walked the octant's node
// list after the octree had been freed (graphics_test crashed like this on every exit, because it
// destroys its OctreeScene without calling Clear() first).
TEST_CASE("OctreeScene can be destroyed while nodes are still in the octree", "[octree_scene]")
{
	EnsureNullDevice();

	// Objects must outlive the scene: nodes detach them when destroyed.
	BoxObject parentObject("Parent");
	BoxObject childObject("Child");
	BoxObject outsideObject("Outside");

	auto scene = std::make_unique<OctreeScene>();

	SceneNode* parent = scene->GetRootSceneNode().CreateChildSceneNode("Parent", Vector3(10.0f, 0.0f, 10.0f));
	parent->AttachObject(parentObject);

	SceneNode* child = parent->CreateChildSceneNode("Child", Vector3(1.0f, 2.0f, 3.0f));
	child->AttachObject(childObject);

	// Outside the octree bounds: forced into the root octant.
	SceneNode* outside = scene->GetRootSceneNode().CreateChildSceneNode("Outside", Vector3(50000.0f, 0.0f, 0.0f));
	outside->AttachObject(outsideObject);

	scene->GetRootSceneNode().Update(true, false);

	REQUIRE(static_cast<OctreeNode*>(parent)->GetOctant() != nullptr);
	REQUIRE(static_cast<OctreeNode*>(child)->GetOctant() != nullptr);
	REQUIRE(static_cast<OctreeNode*>(outside)->GetOctant() != nullptr);

	scene.reset();

	CHECK(!parentObject.IsAttached());
	CHECK(!childObject.IsAttached());
	CHECK(!outsideObject.IsAttached());
}

// Clear() followed by destruction is the path mmo_client and mmo_edit take; it must keep working,
// including for nodes created after the Clear() re-initialised the octree.
TEST_CASE("OctreeScene can be destroyed after Clear with new nodes in the octree", "[octree_scene]")
{
	EnsureNullDevice();

	BoxObject before("Before");
	BoxObject after("After");

	auto scene = std::make_unique<OctreeScene>();

	SceneNode* beforeNode = scene->GetRootSceneNode().CreateChildSceneNode("BeforeNode");
	beforeNode->AttachObject(before);
	scene->GetRootSceneNode().Update(true, false);
	REQUIRE(static_cast<OctreeNode*>(beforeNode)->GetOctant() != nullptr);

	scene->Clear();
	CHECK(!before.IsAttached());

	SceneNode* afterNode = scene->GetRootSceneNode().CreateChildSceneNode("AfterNode");
	afterNode->AttachObject(after);
	scene->GetRootSceneNode().Update(true, false);
	REQUIRE(static_cast<OctreeNode*>(afterNode)->GetOctant() != nullptr);

	scene.reset();

	CHECK(!after.IsAttached());
}
