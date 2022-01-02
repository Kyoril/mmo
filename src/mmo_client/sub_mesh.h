// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "material.h"

#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"

namespace mmo
{
	class Mesh;

	class SubMesh
	{
	public:
		SubMesh(Mesh& parent);

	public:
		void Render() const;
		
	public:
		Mesh& m_parent;
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
		MaterialPtr m_material;
		bool m_useSharedVertices { true };
	};
}
