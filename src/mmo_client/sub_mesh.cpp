// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "sub_mesh.h"
#include "mesh.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"

namespace mmo
{
	SubMesh::SubMesh(Mesh & parent)
		: m_parent(parent)
	{
	}

	void SubMesh::Render() const
	{
		ASSERT(m_vertexBuffer);

		// Activate the material
		if (m_material)
		{
			m_material->Set();
		}
		
		// Setup buffers
		m_vertexBuffer->Set();

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
