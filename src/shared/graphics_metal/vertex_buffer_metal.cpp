// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_buffer_metal.h"
#include "base/macros.h"


namespace mmo
{
	VertexBufferMetal::VertexBufferMetal(GraphicsDeviceMetal & InDevice, size_t InVertexCount, size_t InVertexSize, bool dynamic, const void* InitialData)
		: VertexBuffer(InVertexCount, InVertexSize, dynamic)
		, Device(InDevice)
	{
		m_data.resize(InVertexCount * InVertexSize, 0);
	}

	void * VertexBufferMetal::Map()
	{
		return &m_data[0];
	}

	void VertexBufferMetal::Unmap()
	{
	}

	void VertexBufferMetal::Set()
	{
	}
}
