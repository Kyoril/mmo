// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_buffer_d3d11.h"
#include "base/macros.h"


namespace mmo
{
	VertexBufferD3D11::VertexBufferD3D11(GraphicsDeviceD3D11 & InDevice, size_t InVertexCount, size_t InVertexSize, bool dynamic, const void* InitialData)
		: VertexBuffer(InVertexCount, InVertexSize, dynamic)
		, Device(InDevice)
	{
		// Allocate vertex buffer
		D3D11_BUFFER_DESC BufferDesc;
		ZeroMemory(&BufferDesc, sizeof(BufferDesc));
		BufferDesc.Usage = m_dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
		BufferDesc.ByteWidth = static_cast<UINT>(m_vertexSize * m_vertexCount);
		BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		BufferDesc.CPUAccessFlags = m_dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
		BufferDesc.MiscFlags = 0;

		// Fill buffer with initial data on creation to speed things up
		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = InitialData;
		InitData.SysMemPitch = 0;
		InitData.SysMemSlicePitch = 0;

		ID3D11Device& D3DDevice = Device;
		VERIFY(SUCCEEDED(D3DDevice.CreateBuffer(&BufferDesc, InitialData == nullptr ? nullptr : &InitData, &Buffer)));
	}

	void * VertexBufferD3D11::Map()
	{
		ID3D11DeviceContext& Context = Device;

		D3D11_MAPPED_SUBRESOURCE Sub;
		VERIFY(SUCCEEDED(Context.Map(Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Sub)));

		return Sub.pData;
	}

	void VertexBufferD3D11::Unmap()
	{
		ID3D11DeviceContext& Context = Device;
		Context.Unmap(Buffer.Get(), 0);
	}

	void VertexBufferD3D11::Set()
	{
		ID3D11Buffer* Buffers[1] = { Buffer.Get() };

		UINT Stride = static_cast<UINT>(m_vertexSize);
		UINT Offset = 0;

		ID3D11DeviceContext& Context = Device;
		Context.IASetVertexBuffers(0, ARRAYSIZE(Buffers), Buffers, &Stride, &Offset);
	}

	VertexBufferPtr VertexBufferD3D11::Clone()
	{
		auto buffer = std::make_shared<VertexBufferD3D11>(Device, m_vertexCount, m_vertexSize, m_dynamic);

		ID3D11DeviceContext& Context = Device;
		D3D11_MAPPED_SUBRESOURCE Sub;
		VERIFY(SUCCEEDED(Context.Map(Buffer.Get(), 0, D3D11_MAP_READ, 0, &Sub)));

		void* newBufferData = buffer->Map();
		memcpy(newBufferData, Sub.pData, m_vertexSize * m_vertexCount);
		buffer->Unmap();

		Context.Unmap(Buffer.Get(), 0);

		return std::move(buffer);
	}
}
