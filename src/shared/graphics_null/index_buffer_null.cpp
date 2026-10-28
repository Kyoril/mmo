// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "index_buffer_null.h"

#include <cstring>

namespace mmo
{
	IndexBufferNull::IndexBufferNull(GraphicsDeviceNull & device, size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void* initialData)
		: IndexBuffer(indexCount, indexSize)
		, m_device(device)
	{
		// Kept at the real index size and with the initial contents, so mapping reads back what was
		// written (mesh serialization and geometry merging read buffers back).
		m_indices.resize(indexCount * (indexSize == IndexBufferSize::Index_16 ? sizeof(uint16) : sizeof(uint32)), 0);
		if (initialData && !m_indices.empty())
		{
			std::memcpy(m_indices.data(), initialData, m_indices.size());
		}
	}

	void * IndexBufferNull::Map(LockOptions options)
	{
		return m_indices.data();
	}

	void IndexBufferNull::Unmap()
	{

	}

	void IndexBufferNull::Set(uint16 slot)
	{

	}
}
