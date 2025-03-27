
#include "aabb_tree.h"

#include "ray.h"
#include "base/macros.h"

namespace mmo
{
	namespace
	{
		class ModelFaceSorter
		{
		public:
			ModelFaceSorter(const AABBTree::Vertex* vertices, const AABBTree::Index* indices, uint32 axis)
				: m_vertices(vertices)
				, m_indices(indices)
				, m_axis(axis)
			{
			}

			bool operator ()(uint32 lhs, uint32 rhs) const
			{
				float a = GetCenteroid(lhs);
				float b = GetCenteroid(rhs);
				return (a != b) ? (a < b) : (lhs < rhs);
			}

			float GetCenteroid(uint32 face) const
			{
				auto& a = m_vertices[m_indices[face * 3 + 0]];
				auto& b = m_vertices[m_indices[face * 3 + 1]];
				auto& c = m_vertices[m_indices[face * 3 + 2]];
				return (a[m_axis] + b[m_axis] + c[m_axis]) / 3.0f;
			}

		private:
			const AABBTree::Vertex* m_vertices;
			const AABBTree::Index* m_indices;
			unsigned int m_axis;
		};
	}

	AABBTree::AABBTree(const std::vector<Vertex>& verts, const std::vector<Index>& indices)
	{
		Build(verts, indices);
	}

	void AABBTree::Clear()
	{
		m_vertices.clear();
		m_indices.clear();
		m_faceBounds.clear();
		m_faceIndices.clear();

		m_freeNode = 1;
		m_nodes.clear();
	}

	void AABBTree::Build(const std::vector<Vertex>& verts, const std::vector<Index>& indices)
	{
		m_vertices = verts;
		m_indices = indices;

		m_faceBounds.clear();
		m_faceIndices.clear();

		size_t numFaces = indices.size() / 3;

		m_faceBounds.reserve(numFaces);
		m_faceIndices.reserve(numFaces);

		for (uint32 i = 0; i < (uint32)numFaces; ++i)
		{
			m_faceIndices.push_back(i);
			m_faceBounds.push_back(CalculateFaceBounds(&i, 1));
		}

		m_freeNode = 1;
		m_nodes.clear();
		m_nodes.reserve(int32(numFaces * 1.5f));

		BuildRecursive(0, m_faceIndices.data(), (uint32)numFaces);
		m_faceBounds.clear();

		// Reorder the model indices according to the face indices
		std::vector<Index> sortedIndices(m_indices.size());
		for (size_t i = 0; i < numFaces; ++i)
		{
			uint32 index = m_faceIndices[i] * 3;
			sortedIndices[i * 3 + 0] = m_indices[index + 0];
			sortedIndices[i * 3 + 1] = m_indices[index + 1];
			sortedIndices[i * 3 + 2] = m_indices[index + 2];
		}

		m_indices.swap(sortedIndices);
		m_faceIndices.clear();
	}

	bool AABBTree::IntersectRay(Ray& ray, Index* faceIndex, RaycastFlags flags, Vector3* outHitNormal) const
	{
		const float distance = ray.hitDistance;
		Trace(ray, faceIndex, flags, outHitNormal);
		return ray.hitDistance < distance;
	}

	AABB AABBTree::GetBoundingBox() const
	{
		if (m_nodes.empty())
			return AABB{};
		return m_nodes.front().bounds;
	}

	uint32 AABBTree::PartitionMedian(Node& node, Index* faces, unsigned int numFaces)
	{
		unsigned int axis = GetLongestAxis(node.bounds.GetSize());
		ModelFaceSorter predicate(m_vertices.data(), m_indices.data(), axis);

		std::nth_element(faces, faces + numFaces / 2, faces + numFaces, predicate);
		return numFaces / 2;
	}

	uint32 AABBTree::PartitionSurfaceArea(Node& node, Index* faces, unsigned int numFaces)
	{
		unsigned int bestAxis = 0;
		unsigned int bestIndex = 0;

		float bestCost = std::numeric_limits<float>::max();

		for (unsigned int axis = 0; axis < 3; ++axis)
		{
			ModelFaceSorter predicate(m_vertices.data(), m_indices.data(), axis);
			std::sort(faces, faces + numFaces, predicate);

			// Two passes over data to calculate upper and lower bounds
			std::vector<float> cumulativeLower(numFaces);
			std::vector<float> cumulativeUpper(numFaces);

			AABB lower, upper;

			for (unsigned int i = 0; i < numFaces; ++i)
			{
				lower.Combine(m_faceBounds[faces[i]]);
				upper.Combine(m_faceBounds[faces[numFaces - i - 1]]);

				cumulativeLower[i] = lower.GetSurfaceArea();
				cumulativeUpper[numFaces - i - 1] = upper.GetSurfaceArea();
			}

			float invTotalArea = 1.0f / cumulativeUpper[0];

			// Test split positions
			for (unsigned int i = 0; i < numFaces - 1; ++i)
			{
				float below = cumulativeLower[i] * invTotalArea;
				float above = cumulativeUpper[i] * invTotalArea;

				float cost = 0.125f + (below * i + above * (numFaces - i));
				if (cost <= bestCost)
				{
					bestCost = cost;
					bestIndex = i;
					bestAxis = axis;
				}
			}
		}

		// Sort faces by best axis
		ModelFaceSorter predicate(m_vertices.data(), m_indices.data(), bestAxis);
		std::sort(faces, faces + numFaces, predicate);

		return bestIndex + 1;
	}

	void AABBTree::BuildRecursive(unsigned int nodeIndex, Index* faces, unsigned int numFaces)
	{
		const unsigned int maxFacesPerLeaf = 6;

		// Allocate more nodes if out of nodes
		if (nodeIndex >= m_nodes.size())
		{
			int size = std::max(int(1.5f * m_nodes.size()), 512);
			m_nodes.resize(size);
		}

		auto& node = m_nodes[nodeIndex];
		node.bounds = CalculateFaceBounds(faces, numFaces);

		if (numFaces <= maxFacesPerLeaf)
		{
			node.startFace = static_cast<std::uint32_t>(faces - m_faceIndices.data());
			ASSERT(node.startFace == static_cast<std::size_t>(faces - m_faceIndices.data()));   // verify no truncation
			node.numFaces = numFaces;
		}
		else
		{
			unsigned int leftCount = PartitionSurfaceArea(node, faces, numFaces);
			unsigned int rightCount = numFaces - leftCount;

			// Allocate 2 nodes
			node.children = m_freeNode;
			m_freeNode += 2;

			// Split faces in half and build each side recursively
			BuildRecursive(node.children + 0, faces, leftCount);
			BuildRecursive(node.children + 1, faces + leftCount, rightCount);
		}
	}

	AABB AABBTree::CalculateFaceBounds(Index* faces, unsigned int numFaces) const
	{
		float min = std::numeric_limits<float>::lowest();
		float max = std::numeric_limits<float>::max();

		Vector3 minExtents = { max, max, max };
		Vector3 maxExtents = { min, min, min };

		for (unsigned int i = 0; i < numFaces; ++i)
		{
			auto& v0 = m_vertices[m_indices[faces[i] * 3 + 0]];
			auto& v1 = m_vertices[m_indices[faces[i] * 3 + 1]];
			auto& v2 = m_vertices[m_indices[faces[i] * 3 + 2]];

			minExtents = TakeMinimum(minExtents, v0);
			maxExtents = TakeMaximum(maxExtents, v0);

			minExtents = TakeMinimum(minExtents, v1);
			maxExtents = TakeMaximum(maxExtents, v1);

			minExtents = TakeMinimum(minExtents, v2);
			maxExtents = TakeMaximum(maxExtents, v2);
		}

		return AABB(minExtents, maxExtents);
	}

	void AABBTree::Trace(Ray& ray, Index* faceIndex, RaycastFlags flags, Vector3* outHitNormal) const
	{
		if (m_indices.empty())
			return;

		struct StackEntry
		{
			unsigned int node;
			float dist;
		};

		StackEntry stack[50];
		stack[0].node = 0;
		stack[0].dist = 0.0f;

		float max = std::numeric_limits<float>::max();

		unsigned int stackCount = 1;
		while (!!stackCount)
		{
			// Pop node from back
			StackEntry& e = stack[--stackCount];

			// Ignore if another node has already come closer
			if (e.dist >= ray.hitDistance)
				continue;

			const Node& node = m_nodes.at(e.node);
			if (!node.numFaces)
			{
				// Find closest node
				auto& leftChild = m_nodes.at(node.children + 0);
				auto& rightChild = m_nodes.at(node.children + 1);

				float dist[2] = { max, max };
				auto result1 = ray.IntersectsAABB(leftChild.bounds);
				if (result1.first) dist[0] = result1.second;
				auto result2 = ray.IntersectsAABB(rightChild.bounds);
				if (result2.first) dist[1] = result2.second;

				unsigned int closest = dist[1] < dist[0]; // 0 or 1
				unsigned int furthest = closest ^ 1;

				if (dist[furthest] < ray.hitDistance)
				{
					StackEntry& n = stack[stackCount++];
					n.node = node.children + furthest;
					n.dist = dist[furthest];
				}

				if (dist[closest] < ray.hitDistance)
				{
					StackEntry& n = stack[stackCount++];
					n.node = node.children + closest;
					n.dist = dist[closest];
				}
			}
			else
				if (TraceLeafNode(node, ray, faceIndex, flags, outHitNormal))
					return;
		}
	}

	void AABBTree::TraceRecursive(unsigned int nodeIndex, Ray& ray, Index* faceIndex, RaycastFlags flags) const
	{
		auto& node = m_nodes.at(nodeIndex);
		if (node.numFaces != 0)
			if (TraceLeafNode(node, ray, faceIndex, flags))
				return;
			else
				TraceInnerNode(node, ray, faceIndex, flags);
	}

	void AABBTree::TraceInnerNode(const Node& node, Ray& ray, Index* faceIndex, RaycastFlags flags) const
	{
		auto& leftChild = m_nodes.at(node.children + 0);
		auto& rightChild = m_nodes.at(node.children + 1);

		float max = std::numeric_limits<float>::max();
		float distance[2] = { max, max };

		auto result1 = ray.IntersectsAABB(leftChild.bounds);
		if (result1.first) distance[0] = result1.second;
		auto result2 = ray.IntersectsAABB(rightChild.bounds);
		if (result2.first) distance[1] = result2.second;

		unsigned int closest = 0;
		unsigned int furthest = 1;

		if (distance[1] < distance[0])
			std::swap(closest, furthest);

		if (distance[closest] < ray.hitDistance)
			TraceRecursive(node.children + closest, ray, faceIndex, flags);

		if (distance[furthest] < ray.hitDistance)
			TraceRecursive(node.children + furthest, ray, faceIndex, flags);
	}

	bool AABBTree::TraceLeafNode(const Node& node, Ray& ray, Index* faceIndex, RaycastFlags flags, Vector3* outHitNormal) const
	{
		for (auto i = node.startFace; i < node.startFace + node.numFaces; ++i)
		{
			auto& v0 = m_vertices[m_indices[i * 3 + 0]];
			auto& v1 = m_vertices[m_indices[i * 3 + 1]];
			auto& v2 = m_vertices[m_indices[i * 3 + 2]];

			auto result = ray.IntersectsTriangle(v0, v1, v2, (flags & raycast_flags::IgnoreBackface) != 0);
			if (!result.first)
				continue;

			if (result.second < ray.hitDistance)
			{
				ray.hitDistance = result.second;
				if (faceIndex)
					*faceIndex = i;

				if (outHitNormal)
				{
					// Compute triangle normal.
					Vector3 edge1 = v1 - v0;
					Vector3 edge2 = v2 - v0;
					*outHitNormal = edge1.Cross(edge2);
					outHitNormal->Normalize();
				}

				if (flags & raycast_flags::EarlyExit)
					return true;
			}
		}

		return false;
	}

	uint32 AABBTree::GetLongestAxis(const Vector3& v)
	{
		if (v.x > v.y && v.x > v.z)
			return 0;

		return (v.y > v.z) ? 1 : 2;
	}

	io::Writer& operator<<(io::Writer& w, AABBTree const& tree)
	{
		const uint32 magic = 'BVH1';
		w << io::write<uint32>(magic);

		// Write vertices
		w << io::write_dynamic_range<uint32>(tree.m_vertices);

		// Write indices
		w << io::write_dynamic_range<uint32>(tree.m_indices);

		// Write nodes
		w << io::write<uint32>(tree.m_nodes.size());
		for (const auto& node : tree.m_nodes)
		{
			assert(node.numFaces < 8);

			const std::uint8_t numFaces = static_cast<std::uint8_t>(node.numFaces);
			w << io::write<uint8>(node.numFaces);
			if (node.numFaces != 0)
			{
				// Write leaf node
				w
					<< io::write<uint32>(node.startFace)
					<< node.bounds;
			}
			else
			{
				// Write inner node
				w
					<< io::write<uint32>(node.children)
					<< node.bounds;
			}
		}

		const uint32 endMagic = 'FOOB';
		w << io::write<uint32>(endMagic);

		return w;
	}

	io::Reader& operator>>(io::Reader& r, AABBTree& tree)
	{
		// Map magic
		uint32 magic;
		r >> io::read<uint32>(magic);
		if (magic != static_cast<uint32>('BVH1'))
		{
			r.setFailure();
			return r;
		}

		// Read vertices
		r >> io::read_container<uint32>(tree.m_vertices);

		// Read indices
		r >> io::read_container<uint32>(tree.m_indices);

		// Read nodes
		uint32 nodeCount = 0;
		r >> io::read<uint32>(nodeCount);

		tree.m_nodes.resize(nodeCount);
		for (uint32 i = 0; i < nodeCount; ++i)
		{
			uint8 numFaces = 0;
			r >> io::read<uint8>(numFaces);

			tree.m_nodes[i].numFaces = static_cast<unsigned int>(numFaces);
			if (numFaces != 0)
			{
				// Read leaf node
				r
					>> io::read<uint32>(tree.m_nodes[i].startFace)
					>> tree.m_nodes[i].bounds;
			}
			else
			{
				// Read inner node
				r
					>> io::read<uint32>(tree.m_nodes[i].children)
					>> tree.m_nodes[i].bounds;
			}
		}

		uint32 endMagic;
		r >> io::read<uint32>(endMagic);
		if (endMagic != static_cast<uint32>('FOOB'))
		{
			r.setFailure();
			return r;
		}

		return r;
	}
}
