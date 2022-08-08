// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "index_buffer_metal.h"


namespace mmo
{
	IndexBufferMetal::IndexBufferMetal(GraphicsDeviceMetal & device, size_t indexCount, IndexBufferSize indexSize, const void* initialData)
		: IndexBuffer(indexCount, indexSize)
		, m_device(device)
	{
		m_indices.resize(indexCount);
	}

	void * IndexBufferMetal::Map()
	{
		return &m_indices[0];
	}

	void IndexBufferMetal::Unmap()
	{

	}

	void IndexBufferMetal::Set()
	{

	}
}
