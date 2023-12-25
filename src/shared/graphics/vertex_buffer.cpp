// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_buffer.h"


namespace mmo
{
	VertexBuffer::VertexBuffer(uint32 vertexCount, uint32 vertexSize, BufferUsage usage)
		: m_vertexCount(vertexCount)
		, m_vertexSize(vertexSize)
		, m_usage(usage)
	{
	}
}
