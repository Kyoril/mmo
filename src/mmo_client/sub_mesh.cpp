// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "sub_mesh.h"
#include "mesh.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"

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
		// Activate the material
		if (m_material)
		{
			m_material->Set();
		}

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
}
