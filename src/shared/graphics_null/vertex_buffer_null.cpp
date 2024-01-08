// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_buffer_null.h"
#include "base/macros.h"


namespace mmo
{
	VertexBufferNull::VertexBufferNull(GraphicsDeviceNull & InDevice, size_t InVertexCount, size_t InVertexSize, BufferUsage usage, const void* InitialData)
		: VertexBuffer(InVertexCount, InVertexSize, usage)
		, Device(InDevice)
	{
		m_data.resize(InVertexCount * InVertexSize, 0);
	}

	void * VertexBufferNull::Map(LockOptions lock)
	{
		return &m_data[0];
	}

	void VertexBufferNull::Unmap()
	{
	}

	void VertexBufferNull::Set(uint16 slot)
	{
	}

	VertexBufferPtr VertexBufferNull::Clone()
	{
		return nullptr;
	}
}
