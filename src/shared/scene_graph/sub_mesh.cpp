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
		if (m_material)
		{
			m_material->Apply(GraphicsDevice::Get());
		}

		// Set vertex buffer
		uint32 vertexCount = 0;
		if (m_useSharedVertices)
		{
			ASSERT(m_parent.m_vertexBuffer);
			m_parent.m_vertexBuffer->Set();
			vertexCount = m_parent.m_vertexBuffer->GetVertexCount();

			if (m_parent.m_indexBuffer)
			{
				m_parent.m_indexBuffer->Set();
				GraphicsDevice::Get().DrawIndexed();
			}
			else
			{
				GraphicsDevice::Get().Draw(vertexCount, 0);
			}
		}
		else
		{
			ASSERT(m_vertexBuffer);
			m_vertexBuffer->Set();
			vertexCount = m_vertexBuffer->GetVertexCount();

			if (m_indexBuffer)
			{
				m_indexBuffer->Set();
				GraphicsDevice::Get().DrawIndexed();
			}
			else
			{
				GraphicsDevice::Get().Draw(vertexCount, 0);
			}
		}
	}

	void SubMesh::PrepareRenderOperation(RenderOperation& op) const
	{
		if (m_useSharedVertices)
		{
			ASSERT(m_parent.m_vertexBuffer);
			op.vertexBuffer = m_parent.m_vertexBuffer.get();
			op.indexBuffer = m_parent.m_indexBuffer.get();
			op.useIndexes = m_parent.m_indexBuffer != nullptr;
		}
		else
		{
			ASSERT(m_vertexBuffer);
			op.vertexBuffer = m_vertexBuffer.get();
			op.indexBuffer = m_indexBuffer.get();
			op.useIndexes = m_indexBuffer != nullptr;
		}

		op.topology = TopologyType::TriangleList;
		op.vertexFormat = VertexFormat::PosColorNormalBinormalTangentTex1;
		op.startIndex = m_indexStart;
		op.endIndex = m_indexEnd;
	}

	void SubMesh::SetMaterial(const std::shared_ptr<Material>& material)
	{
		m_material = material;
	}
}
