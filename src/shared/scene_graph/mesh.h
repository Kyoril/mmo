// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "sub_mesh.h"

#include "base/typedefs.h"
#include "graphics/index_buffer.h"
#include "graphics/vertex_buffer.h"
#include "math/aabb.h"
#include "math/quaternion.h"
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

	/// @brief A looping positional sound carried by a mesh asset.
	/// @details Every placement of the mesh plays it at the offset, which follows the placement's
	///          rotation and scale. Playback settings (category, looping, attenuation, fades) come
	///          from the referenced SoundEntry; the emitter only adds where and how loud.
	struct MeshSoundEmitter
	{
		/// @brief Id of the SoundEntry to play.
		uint32 soundEntryId = 0;

		/// @brief Position in mesh-local space.
		Vector3 offset = Vector3::Zero;

		/// @brief Multiplier on the entry's volume.
		float volume = 1.0f;

		/// @brief Audible range override in metres. 0 uses the entry's max distance.
		float maxDistance = 0.0f;
	};

	/// @brief A mesh sound emitter of one placement, resolved to world space.
	struct PlacedSoundEmitter
	{
		uint32 soundEntryId = 0;
		Vector3 position = Vector3::Zero;
		float volume = 1.0f;
		float maxDistance = 0.0f;
	};

	class Mesh;

	/// @brief Appends the sound emitters of a mesh placed with the given transform, in world space.
	void AppendPlacedSoundEmitters(const Mesh& mesh, const Vector3& position, const Quaternion& orientation, const Vector3& scale, std::vector<PlacedSoundEmitter>& out);

	class Mesh final
	{
        friend class SubMesh;
		friend class MeshSerializer;
		friend class MeshDeserializer;

	public:
		typedef std::vector<std::unique_ptr<SubMesh>> SubMeshList;
		typedef std::unordered_map<std::string, uint16> SubMeshNameMap;
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

		/// Removes a sub mesh. Every later sub mesh moves down one index, and its name with it.
		/// @param index Index of the sub mesh to remove.
		void DestroySubMesh(uint16 index);

		/// Removes a sub mesh by name. Does nothing if no sub mesh has this name.
		/// @param name Name of the sub mesh to remove.
		void DestroySubMesh(const std::string& name);

		void SetBounds(const AABB& bounds);

		int32 GetSubMeshIndex(const String& name) const;

        /// Determines whether this mesh has a link to a skeleton resource and thus supports animation.
		[[nodiscard]] bool HasSkeleton() const { return !m_skeletonName.empty(); }

		void SetSkeletonName(const String& skeletonName);

		void SetSkeleton(SkeletonPtr& skeleton);

        [[nodiscard]] const String& GetSkeletonName() const { return m_skeletonName; }

		[[nodiscard]] const SubMeshList& GetSubMeshes() const { return m_subMeshes; }

		[[nodiscard]] uint16 GetSubMeshCount() const { return static_cast<uint16>(m_subMeshes.size()); }

		[[nodiscard]] const AABB& GetBounds() const { return m_aabb; }

		[[nodiscard]] float GetBoundRadius() const { return m_boundRadius; }

        [[nodiscard]] std::string_view GetName() const { return m_name; }

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

		/// @brief Gets the looping positional sounds every placement of this mesh plays.
		[[nodiscard]] const std::vector<MeshSoundEmitter>& GetSoundEmitters() const { return m_soundEmitters; }

		/// @brief Replaces the sound emitters of this mesh.
		void SetSoundEmitters(std::vector<MeshSoundEmitter> emitters) { m_soundEmitters = std::move(emitters); }

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
		AABBTree m_collisionTree {};
		std::vector<MeshSoundEmitter> m_soundEmitters;
	};

	typedef std::shared_ptr<Mesh> MeshPtr;
}
