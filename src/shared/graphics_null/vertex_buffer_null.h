// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "graphics/vertex_buffer.h"
#include "graphics_device_null.h"


namespace mmo
{
	/// Direct3D 11 implementation of a vertex buffer.
	class VertexBufferNull : public VertexBuffer
	{
	public:
		VertexBufferNull(GraphicsDeviceNull& InDevice, size_t VertexCount, size_t VertexSize, BufferUsage usage, const void* InitialData = nullptr);

	public:
		//~Begin CGxBufferBase
		virtual void* Map(LockOptions options) override;
		virtual void Unmap() override;
		virtual void Set(uint16 slot) override;
		VertexBufferPtr Clone() override;
		//~End CGxBufferBase
		
	private:
		GraphicsDeviceNull& Device;
		std::vector<uint8> m_data;
	};
}
