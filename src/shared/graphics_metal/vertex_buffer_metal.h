// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/vertex_buffer.h"
#include "graphics_device_metal.h"


namespace mmo
{
	/// Direct3D 11 implemenation of a vertex buffer.
	class VertexBufferMetal : public VertexBuffer
	{
	public:
        VertexBufferMetal(GraphicsDeviceMetal& InDevice, size_t VertexCount, size_t VertexSize, BufferUsage usage, const void* InitialData = nullptr);

	public:
		//~Begin CGxBufferBase
		virtual void* Map(LockOptions lock) override;
		virtual void Unmap() override;
		virtual void Set(uint16 slot) override;
        virtual std::shared_ptr<VertexBuffer> Clone() override;
		//~End CGxBufferBase
		
	private:
		GraphicsDeviceMetal& Device;
		std::vector<uint8> m_data;
	};
}
