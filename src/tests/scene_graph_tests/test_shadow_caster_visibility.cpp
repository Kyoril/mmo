// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

// math/ray.h (pulled in by the octree headers) uses int32 without the typedefs itself.
#include "base/typedefs.h"

#include "math/aabb.h"
#include "null_device.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/scene_node.h"

#include <algorithm>
#include <vector>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	/// A shadow casting movable object with fixed local bounds, standing in for one group of a
	/// world model (the roof of a house).
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

	bool Contains(const std::vector<MovableObject*>& casters, const MovableObject& object)
	{
		return std::find(casters.begin(), casters.end(), &object) != casters.end();
	}
}

// Portal culling runs once per frame against the *view* camera and hides every world model group
// it does not show. The shadow cascades gather their casters from the octree over a region that
// deliberately reaches well outside the view frustum, because a house behind the camera still
// throws its roof's shadow across the street in front of it. Hiding a group from the view must
// therefore not take it out of the shadow map, or shadows blink out as the player turns.
TEST_CASE("Geometry hidden by portal culling still casts shadows", "[shadow_caster_visibility]")
{
	EnsureNullDevice();

	OctreeScene scene;
	BoxObject roof("Roof", AABB(Vector3(-2.0f, -2.0f, -2.0f), Vector3(2.0f, 2.0f, 2.0f)));
	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode("House", Vector3(10.0f, 10.0f, 10.0f));
	node->AttachObject(roof);
	scene.GetRootSceneNode().Update(true, false);

	const AABB cascadeRegion(Vector3(-50.0f, -50.0f, -50.0f), Vector3(50.0f, 50.0f, 50.0f));
	std::vector<MovableObject*> casters;

	scene.GatherShadowCasters(cascadeRegion, casters);
	REQUIRE(Contains(casters, roof));

	// The camera turns away and portal culling drops the group from the view ...
	roof.SetHiddenByPortalCulling(true);
	CHECK_FALSE(roof.IsVisible());

	// ... but the sun does not care where the camera looks.
	scene.GatherShadowCasters(cascadeRegion, casters);
	CHECK(Contains(casters, roof));
	CHECK(roof.IsVisibleForShadows());

	// Geometry the game hides outright is hidden from the shadow map as well.
	roof.SetHiddenByPortalCulling(false);
	roof.SetVisible(false);
	CHECK_FALSE(roof.IsVisibleForShadows());

	scene.GatherShadowCasters(cascadeRegion, casters);
	CHECK_FALSE(Contains(casters, roof));
}
