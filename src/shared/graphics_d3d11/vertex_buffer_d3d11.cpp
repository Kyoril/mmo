// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_buffer_d3d11.h"
#include "base/macros.h"


namespace mmo
{
	bool IsDynamicUsage(BufferUsage usage)
	{
		return (static_cast<uint32>(usage) & static_cast<uint32>(BufferUsage::Dynamic)) != 0;
	}

	VertexBufferD3D11::VertexBufferD3D11(GraphicsDeviceD3D11 & device, size_t vertexCount, size_t vertexSize, BufferUsage usage, const void* InitialData)
		: VertexBuffer(vertexCount, vertexSize, usage)
		, m_device(device)
	{
		// Allocate vertex buffer
		D3D11_BUFFER_DESC BufferDesc;
		ZeroMemory(&BufferDesc, sizeof(BufferDesc));
		BufferDesc.Usage = IsDynamicUsage(usage) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
		BufferDesc.ByteWidth = static_cast<UINT>(m_vertexSize * m_vertexCount);
		BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		BufferDesc.CPUAccessFlags = IsDynamicUsage(usage) ? D3D11_CPU_ACCESS_WRITE : 0;
		BufferDesc.MiscFlags = 0;

		// Fill buffer with initial data on creation to speed things up
		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = InitialData;
		InitData.SysMemPitch = 0;
		InitData.SysMemSlicePitch = 0;

		ID3D11Device& D3DDevice = m_device;
		VERIFY(SUCCEEDED(D3DDevice.CreateBuffer(&BufferDesc, InitialData == nullptr ? nullptr : &InitData, &m_buffer)));
	}

	void * VertexBufferD3D11::Map(LockOptions lock)
	{
		ID3D11DeviceContext& Context = m_device;

		D3D11_MAPPED_SUBRESOURCE Sub;
		VERIFY(SUCCEEDED(Context.Map(m_buffer.Get(), 0, MapLockOptionsToD3D11(lock), 0, &Sub)));

		return Sub.pData;
	}

	void VertexBufferD3D11::Unmap()
	{
		ID3D11DeviceContext& Context = m_device;
		Context.Unmap(m_buffer.Get(), 0);
	}

	void VertexBufferD3D11::Set(const uint16 slot)
	{
		ID3D11Buffer* buffers[1] = { m_buffer.Get() };

		const UINT stride = m_vertexSize;
		constexpr UINT offset = 0;

		ID3D11DeviceContext& context = m_device;
		context.IASetVertexBuffers(slot, ARRAYSIZE(buffers), buffers, &stride, &offset);
	}

	VertexBufferPtr VertexBufferD3D11::Clone()
	{
		auto buffer = std::make_shared<VertexBufferD3D11>(m_device, m_vertexCount, m_vertexSize, m_usage);

		ID3D11DeviceContext& Context = m_device;
		D3D11_MAPPED_SUBRESOURCE Sub;
		VERIFY(SUCCEEDED(Context.Map(m_buffer.Get(), 0, D3D11_MAP_READ, 0, &Sub)));

		void* newBufferData = buffer->Map(LockOptions::Discard);
		memcpy(newBufferData, Sub.pData, m_vertexSize * m_vertexCount);
		buffer->Unmap();

		Context.Unmap(m_buffer.Get(), 0);

		return std::move(buffer);
	}
}
