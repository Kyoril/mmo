#include "octree_scene.h"
#include "octree_node.h"
#include "camera.h"
#include "log/default_log_levels.h"

namespace mmo
{
	// Implementation of CachedFrustumPlanes
	CachedFrustumPlanes::CachedFrustumPlanes(const Camera& camera)
		: farDistance(camera.GetFarClipDistance())
	{
		// Extract all frustum planes at once for caching
		camera.ExtractFrustumPlanes(&planes[0]);
	}

	AABBVisibility CachedFrustumPlanes::GetVisibility(const AABB& bound) const
	{
		if (bound.IsNull())
		{
			return aabb_visibility::None;
		}

		// Get center of the box
		const Vector3 center = bound.GetCenter();

		// Get the half-size of the box
		const Vector3 halfSize = bound.GetExtents();

		bool allInside = true;

		for (int plane = 0; plane < 6; ++plane)
		{
			// Skip far plane if infinite view frustum
			if (plane == FrustumPlaneFar && farDistance == 0)
				continue;
				
			// Check against each frustum plane
			const Plane::Side side = planes[plane].GetSide(center, halfSize);
			if (side == Plane::NegativeSide) return aabb_visibility::None;

			// We can't return now as the box could be later on the negative side of a plane.
			if (side == Plane::BothSides)
			{
				allInside = false;
			}
		}

		if (allInside)
		{
			return aabb_visibility::Full;
		}

		return aabb_visibility::Partial;
	}
	
	bool CachedFrustumPlanes::IsVisible(const AABB& bound) const
	{
		// Null boxes always invisible
		if (bound.IsNull()) return false;

		const Vector3 center = bound.GetCenter();
		const Vector3 halfSize = bound.GetExtents();

		// For each plane, see if all points are on the negative side
		// If so, object is not visible
		for (int plane = 0; plane < 6; ++plane)
		{
			// Skip far plane if infinite view frustum
			if (plane == FrustumPlaneFar && farDistance == 0)
				continue;

			Plane::Side side = planes[plane].GetSide(center, halfSize);
			if (side == Plane::NegativeSide)
			{
				return false;
			}
		}

		return true;
	}

	OctreeScene::OctreeScene()
	{
		const AABB bounds({ -17100.0f, -17100.0f, -17100.0f }, { 17100.0f, 17100.0f, 17100.0f });
		constexpr size_t maxDepth = 8;

		Init(bounds, maxDepth);
	}

	OctreeScene::OctreeScene(const AABB& box, const size_t maxDepth)
	{
		Init(box, maxDepth);
	}

	OctreeScene::~OctreeScene()
		= default;

	void OctreeScene::Clear()
	{
		Scene::Clear();
		Init(m_bounds, m_maxDepth);
	}

	void OctreeScene::Init(const AABB& box, const size_t maxDepth)
	{
		m_octree = std::make_unique<Octree>(nullptr);

		m_maxDepth = maxDepth;
		m_bounds = box;
		m_octree->m_box = box;

		const Vector3 min = box.min;
		const Vector3 max = box.max;
		m_octree->m_halfSize = (max - min) / 2.0f;
	}

	void OctreeScene::Resize(const AABB& box)
	{
		m_octree = std::make_unique<Octree>(nullptr);
		m_octree->m_box = box;
		m_bounds = box;

		const Vector3 min = box.min;
		const Vector3 max = box.max;
		m_octree->m_halfSize = (max - min) / 2.0f;
	}

	void OctreeScene::UpdateOctreeNode(OctreeNode& node)
	{
		const AABB& box = node.GetWorldAABB();

		if (box.IsNull())
		{
			return;
		}

		// Skip if octree has been destroyed (shutdown conditions)
		if (!m_octree)
		{
			return;
		}

		if (!node.GetOctant())
		{
			// if outside the octree, force into the root node.
			if (!node.IsInAABB(m_octree->m_box))
			{
				m_octree->AddNode(node);
			}
			else
			{
				AddOctreeNode(node, *m_octree);
			}

			return;
		}

		if (!node.IsInAABB(node.GetOctant()->m_box))
		{
			RemoveOctreeNode(node);

			//if outside the octree, force into the root node.
			if (!node.IsInAABB(m_octree->m_box))
			{
				m_octree->AddNode(node);
			}
			else
			{
				AddOctreeNode(node, *m_octree);
			}
		}
	}

	void OctreeScene::RemoveOctreeNode(OctreeNode& node) const
	{
		if (!m_octree)
		{
			return;
		}

		if (Octree* oct = node.GetOctant())
		{
			oct->RemoveNode(node);
		}

		node.SetOctant(nullptr);
	}
	void OctreeScene::AddOctreeNode(OctreeNode& node, Octree& octant, size_t depth)
	{
		if (!m_octree)
		{
			return;
		}

		const AABB& bx = node.GetWorldAABB();

		// Check if the entity spans multiple child octants
		// If so, place it at the current level instead of pushing it down
		bool spansMultipleOctants = false;

		if (depth < m_maxDepth && octant.IsTwiceSize(bx))
		{
			// Calculate which octants the entity would occupy
			const Vector3& octantMin = octant.m_box.min;
			const Vector3& octantMax = octant.m_box.max;
			const Vector3 octantCenter = (octantMin + octantMax) * 0.5f;

			// Check if entity spans across the center dividing planes
			const bool spansX = (bx.min.x < octantCenter.x) && (bx.max.x > octantCenter.x);
			const bool spansY = (bx.min.y < octantCenter.y) && (bx.max.y > octantCenter.y);
			const bool spansZ = (bx.min.z < octantCenter.z) && (bx.max.z > octantCenter.z);

			spansMultipleOctants = spansX || spansY || spansZ;
		}

		// If the octree is twice as big as the scene node AND the node doesn't span multiple octants,
		// add it to a child octant
		if (depth < m_maxDepth && octant.IsTwiceSize(bx) && !spansMultipleOctants)
		{
			int x, y, z;
			octant.GetChildIndices(bx, &x, &y, &z);

			if (!octant.m_children[x][y][z])
			{
				octant.m_children[x][y][z] = std::make_unique<Octree>(&octant);

				const Vector3& octantMin = octant.m_box.min;
				const Vector3& octantMax = octant.m_box.max;
				Vector3 min, max;

				if (x == 0)
				{
					min.x = octantMin.x;
					max.x = (octantMin.x + octantMax.x) / 2.0f;
				}
				else
				{
					min.x = (octantMin.x + octantMax.x) / 2.0f;
					max.x = octantMax.x;
				}

				if (y == 0)
				{
					min.y = octantMin.y;
					max.y = (octantMin.y + octantMax.y) / 2.0f;
				}
				else
				{
					min.y = (octantMin.y + octantMax.y) / 2.0f;
					max.y = octantMax.y;
				}

				if (z == 0)
				{
					min.z = octantMin.z;
					max.z = (octantMin.z + octantMax.z) / 2.0f;
				}
				else
				{
					min.z = (octantMin.z + octantMax.z) / 2.0f;
					max.z = octantMax.z;
				}

				octant.m_children[x][y][z]->m_box.min = min;
				octant.m_children[x][y][z]->m_box.max = max;
				octant.m_children[x][y][z]->m_halfSize = (max - min) / 2.0f;
			}

			AddOctreeNode(node, *octant.m_children[x][y][z], ++depth);
		}
		else
		{
			// Place the entity at this level (either because it's too large for children
			// or because it spans multiple octants)
			octant.AddNode(node);
		}
	}

	void OctreeScene::FindVisibleObjects(Camera& camera, VisibleObjectsBoundsInfo& visibleObjectBounds, bool onlyShadowCasters)
	{
		GetRenderQueue().Clear();

		// Cache the camera frustum planes for this frame to avoid recomputing them
		CachedFrustumPlanes cachedPlanes(camera);
		
		// Walk the octree, adding all visible Octree nodes to the render queue
		WalkOctree(camera, GetRenderQueue(), *m_octree, visibleObjectBounds, false, onlyShadowCasters, cachedPlanes);
	}

	void OctreeScene::WalkOctree(Camera& camera, RenderQueue& queue, Octree& octant, 
		VisibleObjectsBoundsInfo& visibleBounds, const bool foundVisible, 
		const bool onlyShadowCasters, const CachedFrustumPlanes& cachedPlanes)
	{
		if (octant.GetNumNodes() == 0)
		{
			return;
		}

		// Determine visibility using cached frustum planes
		AABBVisibility v = aabb_visibility::None;
		if (foundVisible)
		{
			v = aabb_visibility::Full;
		}
		else if (&octant == m_octree.get())
		{
			v = aabb_visibility::Partial;
		}
		else
		{
			AABB box;
			octant.GetCullBounds(box);
			v = cachedPlanes.GetVisibility(box);
		}

		// Not visible, nothing to do
		if (v == aabb_visibility::None)
		{
			return;
		}

		// Add stuff to be rendered
		bool vis = true;

		// Process nodes in this octant
		auto it = octant.m_nodes.begin();
		while (it != octant.m_nodes.end())
		{
			OctreeNode* sn = *it;

			// if this octree is partially visible, manually cull all
			// scene nodes attached directly to this level.
			if (v == aabb_visibility::Partial)
			{
				vis = cachedPlanes.IsVisible(sn->GetWorldAABB());
			}

			if (vis)
			{
				sn->AddToRenderQueue(camera, queue, visibleBounds, onlyShadowCasters);
			}

			++it;
		}

		// Recursively process child octants
		const bool childFoundVisible = (v == aabb_visibility::Full);
		
		// Calculate camera position relative to octant center for front-to-back traversal
		const Vector3 octantCenter = (octant.m_box.min + octant.m_box.max) * 0.5f;
		const Vector3 camPos = camera.GetDerivedPosition();
		const bool camIsInFront[3] = {
			camPos.x <= octantCenter.x,
			camPos.y <= octantCenter.y,
			camPos.z <= octantCenter.z
		};

		// Visit children in front-to-back order based on camera position
		// This improves culling efficiency by processing closer nodes first
		for (int i = 0; i < 2; ++i)
		{
			const int x = camIsInFront[0] ? i : 1 - i;
			for (int j = 0; j < 2; ++j)
			{
				const int y = camIsInFront[1] ? j : 1 - j;
				for (int k = 0; k < 2; ++k)
				{
					const int z = camIsInFront[2] ? k : 1 - k;
					
					if (Octree* child = octant.m_children[x][y][z].get())
					{
						WalkOctree(camera, queue, *child, visibleBounds, childFoundVisible, onlyShadowCasters, cachedPlanes);
					}
				}
			}
		}
	}

	std::unique_ptr<SceneNode> OctreeScene::CreateSceneNodeImpl()
	{
		return std::make_unique<OctreeNode>(*this);
	}

	std::unique_ptr<SceneNode> OctreeScene::CreateSceneNodeImpl(const String& name)
	{
		return std::make_unique<OctreeNode>(*this, name);
	}

	std::unique_ptr<OctreeRaySceneQuery> OctreeScene::CreateOctreeRayQuery(const Ray& ray)
	{
		auto query = std::make_unique<OctreeRaySceneQuery>(*this);
		query->SetRay(ray);
		return query;
	}

	std::unique_ptr<OctreeAABBSceneQuery> OctreeScene::CreateOctreeAABBQuery(const AABB& box)
	{
		auto query = std::make_unique<OctreeAABBSceneQuery>(*this);
		query->SetBox(box);

		return query;
	}

	std::unique_ptr<RaySceneQuery> OctreeScene::CreateRayQuery(const Ray& ray)
	{
		return CreateOctreeRayQuery(ray);
	}

	std::unique_ptr<AABBSceneQuery> OctreeScene::CreateAABBQuery(const AABB& box)
	{
		return CreateOctreeAABBQuery(box);
	}

	// Implementation of OctreeRaySceneQuery
	OctreeRaySceneQuery::OctreeRaySceneQuery(OctreeScene& scene)
		: RaySceneQuery(scene)
		, m_octreeScene(scene)
	{
	}

	void OctreeRaySceneQuery::Execute(RaySceneQueryListener& listener)
	{
		if (!m_octreeScene.m_octree)
		{
			return;
		}

		// Start the recursive octree traversal from the root
		WalkOctreeForRay(listener, *m_octreeScene.m_octree, GetRay());
	}
	bool OctreeRaySceneQuery::WalkOctreeForRay(RaySceneQueryListener& listener, Octree& octant, const Ray& ray)
	{
		// Early exit if this octant has no nodes
		if (octant.GetNumNodes() == 0)
		{
			return true;
		}

		// Test if the ray intersects this octant's bounding box
		const auto rayIntersection = ray.IntersectsAABB(octant.m_box);
		if (!rayIntersection.first)
		{
			return true; // Ray doesn't intersect this octant, continue with siblings
		}

		// Test all nodes directly attached to this octant
		for (OctreeNode* node : octant.m_nodes)
		{
			// Check all MovableObjects attached to this scene node
			const uint32 numObjects = node->GetNumAttachedObjects();
			for (uint32 i = 0; i < numObjects; ++i)
			{
				MovableObject* movableObj = node->GetAttachedObject(i);
				if (!movableObj)
				{
					continue;
				}

				// Debug notification if enabled
				if (IsDebuggingHitTestResults())
				{
					listener.NotifyObjectChecked(*movableObj);
				}

				// Apply type and query mask filters
				if ((movableObj->GetTypeFlags() & GetQueryTypeMask()) == 0)
				{
					continue;
				}

				if ((movableObj->GetQueryFlags() & GetQueryMask()) == 0)
				{
					continue;
				}

				// Test intersection with the object's world bounding box
				const auto objIntersection = ray.IntersectsAABB(movableObj->GetWorldBoundingBox(true));
				if (!objIntersection.first)
				{
					continue;
				}

				// Report the hit to the listener
				if (!listener.QueryResult(*movableObj, objIntersection.second))
				{
					return false; // Listener requested early termination
				}
			}
		}
		// Recursively process child octants
		// Optimize traversal order based on ray direction for better early termination
		const Vector3& rayDir = ray.GetDirection();
		
		// Determine traversal order based on ray direction
		// Visit octants in the direction the ray is traveling for better culling
		const bool rayGoesPositiveX = rayDir.x > 0.0f;
		const bool rayGoesPositiveY = rayDir.y > 0.0f;
		const bool rayGoesPositiveZ = rayDir.z > 0.0f;

		for (int i = 0; i < 2; ++i)
		{
			const int x = rayGoesPositiveX ? i : 1 - i;
			for (int j = 0; j < 2; ++j)
			{
				const int y = rayGoesPositiveY ? j : 1 - j;
				for (int k = 0; k < 2; ++k)
				{
					const int z = rayGoesPositiveZ ? k : 1 - k;
					
					if (Octree* child = octant.m_children[x][y][z].get())
					{
						if (!WalkOctreeForRay(listener, *child, ray))
						{
							return false; // Early termination requested
						}
					}
				}
			}
		}

		return true; // Continue processing
	}

	// Implementation of OctreeAABBSceneQuery
	OctreeAABBSceneQuery::OctreeAABBSceneQuery(OctreeScene& scene)
		: AABBSceneQuery(scene)
		, m_octreeScene(scene)
	{
	}

	void OctreeAABBSceneQuery::Execute(SceneQueryListener& listener)
	{
		if (!m_octreeScene.m_octree)
		{
			return;
		}

		// Start the recursive octree traversal from the root
		WalkOctreeForAABB(listener, *m_octreeScene.m_octree, GetBox());
	}

	void OctreeAABBSceneQuery::WalkOctreeForAABB(SceneQueryListener& listener, Octree& octant, const AABB& queryAABB)
	{
		// Early exit if this octant has no nodes
		if (octant.GetNumNodes() == 0)
		{
			return;
		}

		// Test if the query AABB intersects this octant's bounding box
		if (!queryAABB.Intersects(octant.m_box))
		{
			return; // No intersection with this octant, skip it
		}

		// Test all nodes directly attached to this octant
		for (OctreeNode* node : octant.m_nodes)
		{
			// Check all MovableObjects attached to this scene node
			const uint32 numObjects = node->GetNumAttachedObjects();
			for (uint32 i = 0; i < numObjects; ++i)
			{
				MovableObject* movableObj = node->GetAttachedObject(i);
				if (!movableObj)
				{
					continue;
				}

				// Apply type and query mask filters
				if ((movableObj->GetTypeFlags() & GetQueryTypeMask()) == 0)
				{
					continue;
				}

				if ((movableObj->GetQueryFlags() & GetQueryMask()) == 0)
				{
					continue;
				}

				const AABB& worldBounds = movableObj->GetWorldBoundingBox(true);

				// Test intersection with the object's world bounding box
				if (!queryAABB.Intersects(worldBounds))
				{
					continue;
				}

				// Report the hit to the listener
				listener.QueryResult(*movableObj);
			}
		}

		// Recursively process child octants
		for (int x = 0; x < 2; ++x)
		{
			for (int y = 0; y < 2; ++y)
			{
				for (int z = 0; z < 2; ++z)
				{
					if (Octree* child = octant.m_children[x][y][z].get())
					{
						WalkOctreeForAABB(listener, *child, queryAABB);
					}
				}
			}
		}
	}
}
