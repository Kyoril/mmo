
#include "octree_node.h"

#include "movable_object.h"

namespace mmo
{
	OctreeNode::OctreeNode(Scene& creator)
		: SceneNode(creator)
	{
	}

	OctreeNode::OctreeNode(Scene& creator, const std::string& name)
		: SceneNode(creator, name)
	{
	}

	OctreeNode::~OctreeNode()
	= default;

	Node* OctreeNode::RemoveChild(uint32 index)
	{
		auto on = static_cast<OctreeNode*>(SceneNode::RemoveChild(index));
		on->RemoveNodeAndChildren();
		return on;
	}

	Node* OctreeNode::RemoveChild(const String& name)
	{
		auto on = static_cast<OctreeNode*>(SceneNode::RemoveChild(name));
		on->RemoveNodeAndChildren();
		return on;
	}

	Node* OctreeNode::RemoveChild(Node& child)
	{
		auto on = static_cast<OctreeNode*>(SceneNode::RemoveChild(child));
		on->RemoveNodeAndChildren();
		return on;
	}

	void OctreeNode::RemoveAllChildren()
	{
		const auto iend = m_children.end();
		for (auto i = m_children.begin(); i != iend; ++i)
		{
			const auto on = static_cast<OctreeNode*>(i->second);
			on->SetParent(nullptr);
			on->RemoveNodeAndChildren();
		}

		m_children.clear();
		m_childrenToUpdate.clear();
	}

	bool OctreeNode::IsInAABB(const AABB& aabb) const
	{
		if (!m_isInSceneGraph || aabb.IsNull()) return false;

		const Vector3 center = m_worldAABB.max.MidPoint(m_worldAABB.min);

		const Vector3 bMin = aabb.min;
		const Vector3 bMax = aabb.max;

		if (const bool isCenter = (bMax > center && bMin < center); !isCenter) return false;

		const Vector3 octreeSize = bMax - bMin;
		const Vector3 nodeSize = m_worldAABB.max - m_worldAABB.min;
		return nodeSize < octreeSize;
	}

	void OctreeNode::UpdateBounds()
	{
		m_worldAABB.SetNull();
		m_localBounds.SetNull();

		// Update bounds from own attached objects
		auto i = m_objectsByName.begin();
		while (i != m_objectsByName.end())
		{
			// Get local bounds of object
			AABB bx = i->second->GetBoundingBox();
			m_localBounds.Combine(bx);

			m_worldAABB.Combine(i->second->GetWorldBoundingBox(true));
			++i;
		}

		if (!m_worldAABB.IsNull() && m_isInSceneGraph)
		{
			//static_cast <OctreeScene&>(m_scene)->UpdateOctreeNode(*this);
		}
	}

	void OctreeNode::RemoveNodeAndChildren()
	{
		//static_cast<OctreeScene&>(m_scene)->RemoveOctreeNode(this);

		auto it = m_children.begin();
		while (it != m_children.end())
		{
			static_cast<OctreeNode*>(it->second)->RemoveNodeAndChildren();
			++it;
		}
	}
}
