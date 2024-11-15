#pragma once
#include <vector>

#include "aabb.h"
#include "base/typedefs.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	struct Ray;

	namespace raycast_flags
	{
		enum Type
		{
			None = 0,

			EarlyExit = 1,

			IgnoreBackface = 2
		};
	}

	typedef raycast_flags::Type RaycastFlags;


	class AABBTree final
	{
		friend io::Writer& operator << (io::Writer& w, AABBTree const& tree);
		friend io::Reader& operator >> (io::Reader& r, AABBTree& tree);

	public:

		/// Represents a vertex in the tree
		typedef Vector3 Vertex;
		typedef uint32 Index;

	private:

		/// 
		struct Node
		{
			union
			{
				/// 
				uint32 children = 0;
				/// 
				uint32 startFace;
			};

			/// 
			uint32 numFaces = 0;

			/// 
			AABB bounds;
		};

	public:

		/// Default constructor.
		AABBTree() = default;

		/// Destructor
		~AABBTree() = default;

		/// Initializes an AABBTree and builds it using the provided vertices and indices.
		/// @param verts Vertices to build.
		/// @param indices Indices to build.
		AABBTree(const std::vector<Vertex>& verts, const std::vector<Index>& indices);

		void Clear();

		/// 
		/// @param verts
		/// @param indices
		void Build(const std::vector<Vertex>& verts, const std::vector<Index>& indices);

		/// 
		/// @param ray 
		/// @param faceIndex
		/// @returns 
		bool IntersectRay(Ray& ray, Index* faceIndex = nullptr, RaycastFlags flags = raycast_flags::None) const;

		/// Gets the bounding box of this tree.
		/// @returns Bounding box of this tree.
		AABB GetBoundingBox() const;

		const std::vector<Node>& GetNodes() const { return m_nodes; }
		const std::vector<Vertex>& GetVertices() const { return m_vertices; }
		const std::vector<Index>& GetIndices() const { return m_indices; }

		bool IsEmpty() const { return m_nodes.empty() && m_vertices.empty() && m_indices.empty(); }

	private:

		/// 
		/// @param node
		/// @param faces
		/// @param numFaces
		/// @returns 
		uint32 PartitionMedian(Node& node, Index* faces, unsigned int numFaces);

		/// 
		/// @param node
		/// @param faces
		/// @param numFaces
		/// @returns 
		uint32 PartitionSurfaceArea(Node& node, Index* faces, unsigned int numFaces);

		/// 
		/// @param nodeIndex
		/// @param faces
		/// @param numFaces
		void BuildRecursive(unsigned int nodeIndex, Index* faces, unsigned int numFaces);

		/// 
		/// @param faces
		/// @param numFaces
		/// @returns 
		AABB CalculateFaceBounds(Index* faces, unsigned int numFaces) const;

		/// 
		/// @param ray
		/// @param faceIndex
		void Trace(Ray& ray, Index* faceIndex, RaycastFlags flags) const;

		/// 
		/// @param nodeIndex
		/// @param ray
		/// @param faceIndex
		void TraceRecursive(unsigned int nodeIndex, Ray& ray, Index* faceIndex, RaycastFlags flags) const;

		/// 
		/// @param node
		/// @param ray
		/// @param faceIndex
		void TraceInnerNode(const Node& node, Ray& ray, Index* faceIndex, RaycastFlags flags) const;

		/// 
		/// @param node
		/// @param ray
		/// @param faceIndex
		bool TraceLeafNode(const Node& node, Ray& ray, Index* faceIndex, RaycastFlags flags) const;

	private:

		/// 
		/// @param v
		/// @returns 
		static uint32 GetLongestAxis(const Vector3& v);

	private:

		uint32 m_freeNode = 0;
		std::vector<Node> m_nodes;
		std::vector<Vertex> m_vertices;
		std::vector<Index> m_indices;
		std::vector<AABB> m_faceBounds;
		std::vector<uint32> m_faceIndices;
	};

	io::Writer& operator << (io::Writer& w, AABBTree const& tree);
	io::Reader& operator >> (io::Reader& r, AABBTree& tree);
}