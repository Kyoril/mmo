#pragma once

#include "scene.h"
#include "octree.h"

namespace mmo
{
	class OctreeNode;

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
		void FindVisibleObjects(Camera& camera, VisibleObjectsBoundsInfo& visibleObjectBounds) override;

		void WalkOctree(Camera&, RenderQueue&, Octree&, VisibleObjectsBoundsInfo& visibleBounds, bool foundVisible, bool onlyShadowCasters);

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
