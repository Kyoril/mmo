#pragma once

#include "scene_node.h"

namespace mmo
{
	class Octree;

	class OctreeNode final : public SceneNode
	{
	public:
		OctreeNode(Scene& creator);

		OctreeNode(Scene& creator, const std::string& name);

		~OctreeNode() override;

	public:

		Node* RemoveChild(uint32 index) override;

		Node* RemoveChild(const String& name) override;

		Node* RemoveChild(Node& child) override;

		void RemoveAllChildren() override;

		Octree* GetOctant() { return m_octant; }

		void SetOctant(Octree* octant) { m_octant = octant; }

		bool IsInAABB(const AABB& aabb) const;

		AABB& GetLocalAABB() { return m_localBounds; }

	protected:

		void UpdateBounds();

		void RemoveNodeAndChildren();

	protected:
		AABB m_localBounds {};

		Octree* m_octant { nullptr };
	};
}
