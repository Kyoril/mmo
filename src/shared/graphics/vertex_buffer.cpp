// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "vertex_buffer.h"


namespace mmo
{
	VertexBuffer::VertexBuffer(uint32 vertexCount, uint32 vertexSize, bool dynamic)
		: m_vertexCount(vertexCount)
		, m_vertexSize(vertexSize)
		, m_dynamic(dynamic)
	{
	}
}
