// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "vertex_buffer_null.h"
#include "base/macros.h"

#include <cstring>

namespace mmo
{
	VertexBufferNull::VertexBufferNull(GraphicsDeviceNull & InDevice, size_t InVertexCount, size_t InVertexSize, BufferUsage usage, const void* InitialData)
		: VertexBuffer(InVertexCount, InVertexSize, usage)
		, Device(InDevice)
	{
		// Keeps the initial contents, so mapping reads back what was written.
		m_data.resize(InVertexCount * InVertexSize, 0);
		if (InitialData && !m_data.empty())
		{
			std::memcpy(m_data.data(), InitialData, m_data.size());
		}
	}

	void * VertexBufferNull::Map(LockOptions lock)
	{
		return m_data.data();
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
