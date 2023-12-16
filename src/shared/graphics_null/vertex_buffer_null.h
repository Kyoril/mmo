// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/vertex_buffer.h"
#include "graphics_device_null.h"


namespace mmo
{
	/// Direct3D 11 implemenation of a vertex buffer.
	class VertexBufferNull : public VertexBuffer
	{
	public:
		VertexBufferNull(GraphicsDeviceNull& InDevice, size_t VertexCount, size_t VertexSize, bool dynamic, const void* InitialData = nullptr);

	public:
		//~Begin CGxBufferBase
		virtual void* Map() override;
		virtual void Unmap() override;
		virtual void Set() override;
		VertexBufferPtr Clone() override;
		//~End CGxBufferBase
		
	private:
		GraphicsDeviceNull& Device;
		std::vector<uint8> m_data;
	};
}
