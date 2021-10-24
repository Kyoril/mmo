// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/vertex_buffer.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	/// Direct3D 11 implemenation of a vertex buffer.
	class VertexBufferD3D11 : public VertexBuffer
	{
	public:
		VertexBufferD3D11(GraphicsDeviceD3D11& InDevice, size_t VertexCount, size_t VertexSize, bool dynamic, const void* InitialData = nullptr);

	public:
		//~Begin CGxBufferBase
		virtual void* Map() override;
		virtual void Unmap() override;
		virtual void Set() override;
		//~End CGxBufferBase

	public:
		inline operator ID3D11Buffer&() const { return *Buffer.Get(); }

	private:
		GraphicsDeviceD3D11& Device;
		ComPtr<ID3D11Buffer> Buffer;
	};
}
