// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graphics/graphics_device.h"
#include "math/aabb.h"
#include "null_device.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	/// A movable object whose local bounds can be rewritten in place, standing in for the

	/// objects that really do this: terrain tiles rebuild their vertex buffer and their height
	/// range whenever the brush deforms them, without ever moving their scene node.
	class ResizableObject final : public MovableObject
	{
	public:
		explicit ResizableObject(const String& name)
			: MovableObject(name)
		{
		}

		/// Rewrites the local bounds the way Tile::UpdateTerrain does, announcing the change.
		void SetBounds(const AABB& bounds)
		{
			m_bounds = bounds;
			InvalidateWorldBounds();
		}

		/// Rewrites the local bounds without announcing it, to show what the derived world box
		/// does when an object forgets (or, as Tile used to, only tells a shadowing member).
		void SetBoundsSilently(const AABB& bounds)
		{
			m_bounds = bounds;
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
		String m_type { "ResizableObject" };
	};
}

// MovableObject derives its world bounding box once and caches it, and for an object that
// never moves nothing else invalidates that cache. Frustum culling reads it every frame
// (RenderQueue::ProcessVisibleObject), so an object whose local bounds grow in place is culled
// against the box it had when it was first drawn until it says otherwise. This is exactly how
// deformed terrain tiles used to disappear: Tile declared its own m_worldAABBDirty, which
// shadowed the base member, so the assignment announcing the new height range went nowhere.

TEST_CASE("World bounding box tracks local bounds after InvalidateWorldBounds", "[movable_bounds]")
{
	EnsureNullDevice();

	Scene scene;
	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode();

	ResizableObject object("Resizable");
	node->AttachObject(object);

	// Prime the cache the way the first frame of culling would.
	CHECK(object.GetWorldBoundingBox(true).max.y == Approx(1.0f));

	object.SetBounds(AABB(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 250.0f, 1.0f)));

	CHECK(object.GetWorldBoundingBox(true).max.y == Approx(250.0f));
}

TEST_CASE("World bounding box stays stale until the change is announced", "[movable_bounds]")
{
	EnsureNullDevice();

	Scene scene;
	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode();

	ResizableObject object("ResizableSilent");
	node->AttachObject(object);

	CHECK(object.GetWorldBoundingBox(true).max.y == Approx(1.0f));

	object.SetBoundsSilently(AABB(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 250.0f, 1.0f)));

	// The regression this pins: the derived box keeps the old height range, and a camera that
	// can only see the new geometry culls the object away entirely.
	CHECK(object.GetWorldBoundingBox(true).max.y == Approx(1.0f));

	// Announcing it is what recovers the correct box.
	object.SetBounds(object.GetBoundingBox());
	CHECK(object.GetWorldBoundingBox(true).max.y == Approx(250.0f));
}

TEST_CASE("Deriving the world bounding box twice is stable", "[movable_bounds]")
{
	EnsureNullDevice();

	Scene scene;
	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode();
	node->SetPosition(Vector3(10.0f, 0.0f, -5.0f));

	ResizableObject object("ResizableTransformed");
	node->AttachObject(object);

	const AABB first = object.GetWorldBoundingBox(true);
	const AABB second = object.GetWorldBoundingBox(true);

	CHECK(first.min.x == Approx(second.min.x));
	CHECK(first.max.x == Approx(second.max.x));
	CHECK(first.min.x == Approx(9.0f));
	CHECK(first.max.z == Approx(-4.0f));
}
