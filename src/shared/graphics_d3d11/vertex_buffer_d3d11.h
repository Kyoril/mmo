// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "graphics/vertex_buffer.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	/// Direct3D 11 implementation of a vertex buffer.
	class VertexBufferD3D11 final : public VertexBuffer
	{
	public:
		VertexBufferD3D11(GraphicsDeviceD3D11& device, uint32 vertexCount, uint32 vertexSize, BufferUsage usage, const void* initialData = nullptr);

	public:
		//~Begin CGxBufferBase
		void* Map(LockOptions lock) override;
		void Unmap() override;
		void Set(uint16 slot) override;
		VertexBufferPtr Clone() override;
		//~End CGxBufferBase

	public:
		operator ID3D11Buffer&() const { return *m_buffer.Get(); }

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11Buffer> m_buffer;
		ComPtr<ID3D11Buffer> m_tempStagingBuffer;
	};
}
