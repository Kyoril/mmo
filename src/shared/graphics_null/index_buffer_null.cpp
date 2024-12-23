// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "index_buffer_null.h"


namespace mmo
{
	IndexBufferNull::IndexBufferNull(GraphicsDeviceNull & device, size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void* initialData)
		: IndexBuffer(indexCount, indexSize)
		, m_device(device)
	{
		m_indices.resize(indexCount);
	}

	void * IndexBufferNull::Map(LockOptions options)
	{
		return &m_indices[0];
	}

	void IndexBufferNull::Unmap()
	{

	}

	void IndexBufferNull::Set(uint16 slot)
	{

	}
}
