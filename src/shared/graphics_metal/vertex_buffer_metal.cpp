// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_buffer_metal.h"
#include "base/macros.h"


namespace mmo
{
	VertexBufferMetal::VertexBufferMetal(GraphicsDeviceMetal & InDevice, size_t InVertexCount, size_t InVertexSize, BufferUsage usage, const void* InitialData)
		: VertexBuffer(InVertexCount, InVertexSize, usage)
		, Device(InDevice)
	{
		m_data.resize(InVertexCount * InVertexSize, 0);
	}

	void * VertexBufferMetal::Map(LockOptions lock)
	{
		return &m_data[0];
	}

	void VertexBufferMetal::Unmap()
	{
	}

	void VertexBufferMetal::Set(uint16 slot)
	{
	}

    std::shared_ptr<VertexBuffer> VertexBufferMetal::Clone()
    {
        return nullptr;
    }
}
