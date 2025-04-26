#pragma once

#include "scene.h"
#include "octree.h"
#include "math/plane.h"
#include <array>

namespace mmo
{
	class OctreeNode;
	class Camera;

	// Cache for camera frustum planes to avoid recalculating them for each node
	struct CachedFrustumPlanes 
	{
		std::array<Plane, 6> planes;
		float farDistance;
		
		// Extract and cache the frustum planes from a camera
		explicit CachedFrustumPlanes(const Camera& camera);
		
		// Check if an AABB is visible using the cached planes
		AABBVisibility GetVisibility(const AABB& bound) const;
		
		// Check if an AABB is visible (simple bool version)
		bool IsVisible(const AABB& bound) const;
	};

	class OctreeScene : public Scene
	{
	public:
		OctreeScene();

		OctreeScene(const AABB& box, size_t maxDepth);

		~OctreeScene() override;

	public:
		void Clear() override;

	public:
		void Init(const AABB& box, size_t maxDepth);

		void Resize(const AABB& box);

		void UpdateOctreeNode(OctreeNode& node);

		void RemoveOctreeNode(OctreeNode& node) const;

		void AddOctreeNode(OctreeNode& node, Octree& octant, size_t depth = 0);

	protected:
		void FindVisibleObjects(Camera& camera, VisibleObjectsBoundsInfo& visibleObjectBounds, bool onlyShadowCasters) override;

		void WalkOctree(Camera& camera, RenderQueue& queue, Octree& octant, VisibleObjectsBoundsInfo& visibleBounds, 
			bool foundVisible, bool onlyShadowCasters, const CachedFrustumPlanes& cachedPlanes);

		std::unique_ptr<SceneNode> CreateSceneNodeImpl() override;

		std::unique_ptr<SceneNode> CreateSceneNodeImpl(const String& name) override;

	protected:
		/// The root octree
		std::unique_ptr<Octree> m_octree;

		/// Max depth for the tree
		size_t m_maxDepth;

		/// Size of the octree
		AABB m_bounds;
	};
}
