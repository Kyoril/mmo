// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "sub_mesh.h"

#include "material_manager.h"
#include "mesh.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "scene_graph/render_operation.h"

namespace mmo
{
	SubMesh::SubMesh(Mesh & parent)
		: parent(parent)
	{
	}

	void SubMesh::PrepareRenderOperation(RenderOperation& op) const
	{
		if (useSharedVertices)
		{
			ASSERT(parent.sharedVertexData);
			op.vertexData = parent.sharedVertexData.get();
		}
		else
		{
			ASSERT(vertexData);
			op.vertexData = vertexData.get();
		}

		op.indexData = indexData.get();
		op.topology = m_topologyType;
		op.vertexFormat = VertexFormat::PosColorNormalBinormalTangentTex1;
		op.material = m_material;
	}

	void SubMesh::SetMaterialName(const String& name)
	{
		SetMaterial(MaterialManager::Get().Load(name));
	}

	void SubMesh::SetMaterial(const MaterialPtr& material)
	{
		m_material = material;
	}

	void SubMesh::CompileBoneAssignments()
	{
		ASSERT(vertexData);

		const uint16 maxBones = parent.NormalizeBoneAssignments(vertexData->vertexCount, m_boneAssignments);
		if (maxBones != 0)
		{
			Mesh::CompileBoneAssignments(m_boneAssignments, maxBones, blendIndexToBoneIndexMap, vertexData.get());
		}

		m_boneAssignmentsOutOfDate = false;
	}

	void SubMesh::AddBoneAssignment(const VertexBoneAssignment& vertBoneAssign)
	{
		ASSERT(!useSharedVertices);

		m_boneAssignments.insert(
			VertexBoneAssignmentList::value_type(vertBoneAssign.vertexIndex, vertBoneAssign)
		);
		m_boneAssignmentsOutOfDate = true;
	}

	void SubMesh::ClearBoneAssignments()
	{
		m_boneAssignments.clear();
		m_boneAssignmentsOutOfDate = true;
	}
}
