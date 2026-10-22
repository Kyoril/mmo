// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "mesh.h"
#include "world_model_merge.h"

#include "math/matrix4.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace mmo
{
	class SubMesh;
	class VertexData;

	/// @brief Default vertex budget of one merged submesh; a bucket above it continues in the next part.
	constexpr size_t MaxMergedSubMeshVertices = 1000000;

	/// @brief CPU copy of one submesh's geometry, read back from its GPU buffers.
	struct MeshMergeGeometry
	{
		std::vector<WorldModelMergeVertex> vertices;
		std::vector<uint32> indices;
	};

	/// @brief Merged geometry being collected for one material.
	struct MeshMergeBucket
	{
		/// @brief The material the merged submesh uses (may be null: the serializer then writes the default material).
		MaterialPtr material;
		std::vector<WorldModelMergeVertex> vertices;
		std::vector<uint32> indices;
	};

	/// @brief Merge buckets by material name.
	using MeshMergeBuckets = std::map<String, MeshMergeBucket>;

	/// @brief Whether vertex data uses the standard static mesh layout (WorldModelMergeVertex in one
	///        buffer, at the offsets MeshDeserializer creates, without blend indices).
	[[nodiscard]] bool HasMergeableVertexLayout(const VertexData& vertexData);

	/// @brief Whether a mesh's geometry can be merged: no skeleton, and every submesh has index data
	///        and its own vertex data in the standard static mesh vertex layout.
	[[nodiscard]] bool CanMergeMesh(const Mesh& mesh);

	/// @brief Reads a submesh's vertices and indices back from its GPU buffers.
	/// @param subMesh A submesh with its own vertex data in the mergeable layout.
	/// @param out Receives the geometry; indices are widened to 32 bit.
	/// @return False when a buffer is missing or cannot be mapped.
	bool ReadSubMeshGeometry(const SubMesh& subMesh, MeshMergeGeometry& out);

	/// @brief Creates a mesh with one submesh per non-empty bucket, in the static mesh vertex layout.
	/// @details Submeshes are named "m<index>" so their names are never empty. Index buffers are 16 bit
	///          when the vertices allow it. The bounds cover every vertex.
	/// @param name Name of the new (unmanaged) mesh.
	/// @param buckets The geometry by material. Left unchanged.
	/// @return The mesh, or null when every bucket is empty.
	MeshPtr CreateMergedMesh(const String& name, MeshMergeBuckets& buckets);

	/// @brief Submesh geometry read back once per distinct submesh, however often it is placed.
	class MeshReadBackCache final
	{
	public:
		/// @brief Returns the geometry of a submesh, reading it back on first use.
		/// @return Null when the submesh cannot be read back.
		const MeshMergeGeometry* Get(const Mesh& mesh, uint16 subMeshIndex);

		/// @brief Drops every cached copy.
		void Clear() { m_entries.clear(); }

	private:
		/// Null geometry marks a submesh that could not be read, so it is not tried again.
		std::map<std::pair<const Mesh*, uint16>, std::unique_ptr<MeshMergeGeometry>> m_entries;
	};

	/// @brief Merges placed meshes into buckets by material, split into parts that stay within a vertex
	///        budget per bucket.
	/// @details Part n holds the n-th piece of every material bucket: a material whose geometry exceeds
	///          the budget continues in the next part, so each part becomes one mesh.
	class MeshMergeAccumulator final
	{
	public:
		/// @param cache Read-back geometry, shared between accumulators that place the same meshes.
		/// @param maxVerticesPerBucket Vertex budget of one bucket. A single submesh larger than it
		///        still goes into one (otherwise empty) bucket.
		explicit MeshMergeAccumulator(MeshReadBackCache& cache, size_t maxVerticesPerBucket = MaxMergedSubMeshVertices);

		/// @brief Appends a mesh, transformed, to the buckets of its submeshes' materials.
		/// @param mesh A mergeable mesh (see CanMergeMesh).
		/// @param transform Mesh space to the merged space. Placements with a non-finite transform are refused.
		/// @param materialOverrides Material per submesh (by index); a missing or empty entry keeps the
		///        submesh's own material.
		/// @return False when the transform is not finite or a submesh could not be read back (submeshes
		///         merged before that stay merged).
		bool Add(const Mesh& mesh, const Matrix4& transform, const std::vector<String>& materialOverrides = {});

		/// @brief The merged parts.
		[[nodiscard]] std::vector<MeshMergeBuckets>& GetParts() { return m_parts; }

		/// @brief The merged parts.
		[[nodiscard]] const std::vector<MeshMergeBuckets>& GetParts() const { return m_parts; }

	private:
		MeshMergeBucket& BucketFor(const String& materialName, size_t vertexCount);

		MeshReadBackCache& m_cache;
		size_t m_maxVertices;
		std::vector<MeshMergeBuckets> m_parts;
	};
}
