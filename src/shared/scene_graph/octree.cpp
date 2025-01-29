
#include "octree.h"

#include "octree_node.h"

namespace mmo
{
	Octree::Octree(Octree* parent)
	{
		m_parent = parent;
		m_numNodes = 0;
	}

	Octree::~Octree()
	{
		// Delete all children
		for (auto& x : m_children)
		{
			for (auto& y : x)
			{
				for (auto& z : y)
				{
					z.reset();
				}
			}
		}
	}

	void Octree::AddNode(OctreeNode& node)
	{
		m_nodes.push_back(&node);
		node.SetOctant(this);

		Ref();
	}

	void Octree::RemoveNode(OctreeNode& node)
	{
		m_nodes.remove(&node);
		node.SetOctant(nullptr);

		DeRef();
	}

	bool Octree::IsTwiceSize(const AABB& box) const
	{
		if (box.IsNull())
		{
			return false;
		}

		const Vector3 halfMBoxSize = m_box.GetExtents();
		const Vector3 boxSize = box.GetSize();
		return boxSize.x <= halfMBoxSize.x && boxSize.y <= halfMBoxSize.y && boxSize.z <= halfMBoxSize.z;
	}

	void Octree::GetChildIndices(const AABB& box, int32* x, int32* y, int32* z) const
	{
		const Vector3 center = m_box.max.MidPoint(m_box.min);
		const Vector3 ncenter = box.max.MidPoint(box.min);

		if (ncenter.x > center.x)
		{
			*x = 1;
		}
		else
		{
			*x = 0;
		}

		if (ncenter.y > center.y)
		{
			*y = 1;
		}
		else
		{
			*y = 0;
		}

		if (ncenter.z > center.z)
		{
			*z = 1;
		}
		else
		{
			*z = 0;
		}
	}

	void Octree::GetCullBounds(AABB& out) const
	{
		out.min = m_box.min - m_halfSize;
		out.max = m_box.max + m_halfSize;
	}
}
