// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "vertex_buffer.h"


namespace mmo
{
	VertexBuffer::VertexBuffer(size_t InVertexCount, size_t InVertexSize, bool dynamic)
		: VertexCount(InVertexCount)
		, VertexSize(InVertexSize)
		, m_dynamic(dynamic)
	{
	}
}
