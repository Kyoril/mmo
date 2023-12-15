// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "sub_mesh.h"

#include "base/typedefs.h"
#include "graphics/index_buffer.h"
#include "graphics/vertex_buffer.h"
#include "math/aabb.h"
#include "skeleton.h"

#include <map>
#include <memory>
#include <vector>

#include "graphics/constant_buffer.h"

namespace mmo
{
	struct VertexBoneAssignment
	{
		unsigned int vertexIndex;
		unsigned short boneIndex;
		float weight;
	};

	class Mesh final
	{
        friend class SubMesh;
		friend class MeshSerializer;

	public:
		typedef std::vector<std::unique_ptr<SubMesh>> SubMeshList;
		typedef std::map<std::string, uint16> SubMeshNameMap;
		typedef std::multimap<size_t, VertexBoneAssignment> VertexBoneAssignmentList;

	public:
        explicit Mesh(String name)
			: m_name(std::move(name))
		{
		}

	public: 
		SubMesh& CreateSubMesh();

		SubMesh& CreateSubMesh(const std::string& name);

		void NameSubMesh(uint16 index, const std::string& name);

		SubMesh& GetSubMesh(uint16 index);

		SubMesh* GetSubMesh(const std::string& name);

		void DestroySubMesh(uint16 index);

		void DestroySubMesh(const std::string& name);

		void SetBounds(const AABB& bounds);

		void Render();

		/// Determines whether this mesh has a link to a skeleton resource and thus supports animation.
		[[nodiscard]] bool HasSkeleton() const noexcept { return !m_skeletonName.empty(); }

		void SetSkeletonName(const String& skeletonName);

		const String& GetSkeletonName() const { return m_skeletonName; }

		const SubMeshList& GetSubMeshes() const noexcept { return m_subMeshes; }

		uint16 GetSubMeshCount() const noexcept { return static_cast<uint16>(m_subMeshes.size()); }

		const AABB& GetBounds() const noexcept { return m_aabb; }

		float GetBoundRadius() const noexcept { return m_boundRadius; }

        [[nodiscard]] std::string_view GetName() const noexcept { return m_name; }

		void AddBoneAssignment(const VertexBoneAssignment& vertBoneAssign);

		void ClearBoneAssignments();

		void NotifySkeleton(SkeletonPtr& skeleton);

		const SkeletonPtr& GetSkeleton() const { return m_skeleton; }

		const VertexBoneAssignmentList& GetBoneAssignments() const { return m_boneAssignments; }

	public:
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
		ConstantBufferPtr m_boneMatricesBuffer;

	private:
		SubMeshList m_subMeshes;
		SubMeshNameMap m_subMeshNames;
		AABB m_aabb;
		float m_boundRadius { 0.0f };
		String m_skeletonName;
		String m_name;
		SkeletonPtr m_skeleton{ nullptr };
		VertexBoneAssignmentList m_boneAssignments;
		bool m_boneAssignmentsOutOfDate { false };
		std::vector<Matrix4> m_boneMatrices;
	};

	typedef std::shared_ptr<Mesh> MeshPtr;
}
