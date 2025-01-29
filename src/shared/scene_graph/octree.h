#pragma once

#include <list>

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "math/aabb.h"
#include "math/vector3.h"

namespace mmo
{
	class OctreeNode;

	class Octree final : public NonCopyable
	{
	public:
		Octree(Octree* parent);
		~Octree() override;

	public:
        /// Adds an Octree scene node to this octree level.
        void AddNode(OctreeNode& node);

		void RemoveNode(OctreeNode& node);

        [[nodiscard]] size_t GetNumNodes() const { return m_numNodes; }

	public:
        AABB m_box;

        Vector3 m_halfSize;

        std::unique_ptr<Octree> m_children[2][2][2];

	public:
		bool IsTwiceSize(const AABB& box) const;

		void GetChildIndices(const AABB& box, int32* x, int32* y, ::int32* z) const;

        void GetCullBounds(AABB& out) const;


        typedef std::list<OctreeNode*> NodeList;
        NodeList m_nodes;

    protected:
        void Ref()
        {
            m_numNodes++;

            if (m_parent) m_parent->Ref();
        }

        void DeRef()
        {
            m_numNodes--;

            if (m_parent) m_parent->DeRef();
        }

        size_t m_numNodes{ 0 };
        Octree* m_parent { nullptr };
	};
}
