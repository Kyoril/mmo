// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "vertex_buffer_d3d11.h"
#include "base/macros.h"


namespace mmo
{
	VertexBufferD3D11::VertexBufferD3D11(GraphicsDeviceD3D11 & InDevice, size_t InVertexCount, size_t InVertexSize, const void* InitialData)
		: VertexBuffer(InVertexCount, InVertexSize)
		, Device(InDevice)
	{
		// Allocate vertex buffer
		D3D11_BUFFER_DESC BufferDesc;
		ZeroMemory(&BufferDesc, sizeof(BufferDesc));
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.ByteWidth = static_cast<UINT>(VertexSize * VertexCount);
		BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
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

		UINT Stride = static_cast<UINT>(VertexSize);
		UINT Offset = 0;

		ID3D11DeviceContext& Context = Device;
		Context.IASetVertexBuffers(0, ARRAYSIZE(Buffers), Buffers, &Stride, &Offset);
	}
}
