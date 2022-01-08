// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"

namespace mmo
{
	class RenderOperation;
	class Mesh;

	class SubMesh
	{
	public:
		SubMesh(Mesh& parent);

	public:
		void Render() const;

		void PrepareRenderOperation(RenderOperation& op) const;
		
	public:
		Mesh& m_parent;
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
		bool m_useSharedVertices { true };
	};
}
