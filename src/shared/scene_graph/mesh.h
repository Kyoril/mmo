// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
#include "math/aabb_tree.h"

namespace mmo
{
	class VertexData;

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
		friend class MeshDeserializer;

	public:
		typedef std::vector<std::unique_ptr<SubMesh>> SubMeshList;
		typedef std::map<std::string, uint16> SubMeshNameMap;
		typedef std::multimap<size_t, VertexBoneAssignment> VertexBoneAssignmentList;
		typedef std::vector<uint16> IndexMap;

	public:
        explicit Mesh(String name)
			: m_name(std::move(name))
		{
		}

	public: 
		SubMesh& CreateSubMesh();

		SubMesh& CreateSubMesh(const std::string& name);

		void NameSubMesh(uint16 index, const std::string& name);

		bool GetSubMeshName(uint16 index, String& name) const;

		SubMesh& GetSubMesh(uint16 index) const;

		SubMesh* GetSubMesh(const std::string& name);

		void DestroySubMesh(uint16 index);

		void DestroySubMesh(const std::string& name);

		void SetBounds(const AABB& bounds);

		/// Determines whether this mesh has a link to a skeleton resource and thus supports animation.
		[[nodiscard]] bool HasSkeleton() const noexcept { return !m_skeletonName.empty(); }

		void SetSkeletonName(const String& skeletonName);

		void SetSkeleton(SkeletonPtr& skeleton);

        [[nodiscard]] const String& GetSkeletonName() const { return m_skeletonName; }

		[[nodiscard]] const SubMeshList& GetSubMeshes() const noexcept { return m_subMeshes; }

		[[nodiscard]] uint16 GetSubMeshCount() const noexcept { return static_cast<uint16>(m_subMeshes.size()); }

		[[nodiscard]] const AABB& GetBounds() const noexcept { return m_aabb; }

		[[nodiscard]] float GetBoundRadius() const noexcept { return m_boundRadius; }

        [[nodiscard]] std::string_view GetName() const noexcept { return m_name; }

		void AddBoneAssignment(const VertexBoneAssignment& vertBoneAssign);

		void ClearBoneAssignments();

		void NotifySkeleton(const SkeletonPtr& skeleton);

		[[nodiscard]] const SkeletonPtr& GetSkeleton() const { return m_skeleton; }

		[[nodiscard]] const VertexBoneAssignmentList& GetBoneAssignments() const { return m_boneAssignments; }

		uint16 NormalizeBoneAssignments(uint64 vertexCount, VertexBoneAssignmentList& assignments) const;

		void CompileBoneAssignments();

		void UpdateCompiledBoneAssignments();

        void InitAnimationState(AnimationStateSet& animationState);

		AABBTree& GetCollisionTree() { return m_collisionTree; }

    protected:
		bool m_boneAssignmentsOutOfDate { false };

        static void BuildIndexMap(const VertexBoneAssignmentList& boneAssignments, IndexMap& boneIndexToBlendIndexMap, IndexMap& blendIndexToBoneIndexMap);

        static void CompileBoneAssignments(const VertexBoneAssignmentList& boneAssignments, uint16 numBlendWeightsPerVertex, IndexMap& blendIndexToBoneIndexMap, const VertexData* targetVertexData);

	public:
		ConstantBufferPtr m_boneMatricesBuffer;
		std::unique_ptr<VertexData> sharedVertexData{nullptr};
		IndexMap sharedBlendIndexToBoneIndexMap{};

	private:
		SubMeshList m_subMeshes;
		SubMeshNameMap m_subMeshNames;
		AABB m_aabb;
		float m_boundRadius { 0.0f };
		String m_skeletonName;
		String m_name;
		SkeletonPtr m_skeleton{ nullptr };
		VertexBoneAssignmentList m_boneAssignments;
		std::vector<Matrix4> m_boneMatrices;
		AABBTree m_collisionTree;
	};

	typedef std::shared_ptr<Mesh> MeshPtr;
}
