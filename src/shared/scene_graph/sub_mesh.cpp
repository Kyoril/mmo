// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "sub_mesh.h"
#include "mesh.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "scene_graph/render_operation.h"

namespace mmo
{
	SubMesh::SubMesh(Mesh & parent)
		: m_parent(parent)
		, m_vertexBuffer(nullptr)
		, m_indexBuffer(nullptr)
	{
	}

	void SubMesh::Render() const
	{
		// Set vertex buffer
		if (m_useSharedVertices)
		{
			ASSERT(m_parent.m_vertexBuffer);
			m_parent.m_vertexBuffer->Set();
		}
		else
		{
			ASSERT(m_vertexBuffer);
			m_vertexBuffer->Set();
		}

		// Render
		if (m_indexBuffer)
		{
			m_indexBuffer->Set();
			GraphicsDevice::Get().DrawIndexed();
		}
		else
		{
			GraphicsDevice::Get().Draw(m_vertexBuffer->GetVertexCount(), 0);
		}
	}

	void SubMesh::PrepareRenderOperation(RenderOperation& op) const
	{
		if (m_useSharedVertices)
		{
			ASSERT(m_parent.m_vertexBuffer);
			op.vertexBuffer = m_parent.m_vertexBuffer.get();
		}
		else
		{
			ASSERT(m_vertexBuffer);
			op.vertexBuffer = m_vertexBuffer.get();
		}

		op.indexBuffer = m_indexBuffer.get();
		op.useIndexes = m_indexBuffer != nullptr;
		op.topology = TopologyType::TriangleList;
		op.vertexFormat = VertexFormat::PosColorNormalTex1;
		op.startIndex = m_indexStart;
		op.endIndex = m_indexEnd;
	}

	void SubMesh::SetMaterial(const std::shared_ptr<Material>& material)
	{
		m_material = material;
	}
}
