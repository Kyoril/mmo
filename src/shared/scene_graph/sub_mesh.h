// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"
#include "graphics/material.h"
#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"
#include "graphics/vertex_index_data.h"

namespace mmo
{
	struct VertexBoneAssignment;
	class RenderOperation;
	class Mesh;

	/// @brief This class is a renderable part of a mesh with it's own material id.
	class SubMesh
	{
	public:
		/// @brief Creates a new instance of the SubMesh class and initializes it.
		/// @param parent The Mesh parent that this sub mesh belongs to.
		SubMesh(Mesh& parent);

	public:
		/// @brief Prepares a render operation in order to be able to render the sub mesh.
		/// @param op The render operation to setup.
		void PrepareRenderOperation(RenderOperation& op) const;

		/// @brief Gets the material assigned to this sub mesh.
		[[nodiscard]] MaterialPtr& GetMaterial() noexcept { return m_material; }

		void SetMaterialName(const String& name);

		/// @brief Sets the material used by this sub mesh.
		/// @param material The new material to use or nullptr if no material should be used.
		void SetMaterial(const MaterialPtr& material);

		void AddBoneAssignment(const VertexBoneAssignment& vertBoneAssign);

		void ClearBoneAssignments();

		typedef std::multimap<size_t, VertexBoneAssignment> VertexBoneAssignmentList;
		const VertexBoneAssignmentList& GetBoneAssignments() const { return m_boneAssignments; }

		void CompileBoneAssignments();

		void SetTopologyType(TopologyType type) { m_topologyType = type; }

	public:
		Mesh& parent;

		std::unique_ptr<VertexData> vertexData{nullptr};
		std::unique_ptr<IndexData> indexData{nullptr};

		typedef std::vector<uint16> IndexMap;
		IndexMap blendIndexToBoneIndexMap{};

		MaterialPtr m_material;
		bool useSharedVertices { true };
		bool m_boneAssignmentsOutOfDate{false};

		TopologyType m_topologyType = TopologyType::TriangleList;

		VertexBoneAssignmentList m_boneAssignments{};
	};
}
