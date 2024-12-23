// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "vertex_buffer_d3d11.h"
#include "base/macros.h"


namespace mmo
{
	bool IsDynamicUsage(BufferUsage usage)
	{
		return (static_cast<uint32>(usage) & static_cast<uint32>(BufferUsage::Dynamic)) != 0;
	}

	VertexBufferD3D11::VertexBufferD3D11(GraphicsDeviceD3D11 & device, const uint32 vertexCount, const uint32 vertexSize, const BufferUsage usage, const void* initialData)
		: VertexBuffer(vertexCount, vertexSize, usage)
		, m_device(device)
	{
		// Allocate vertex buffer
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.Usage = IsDynamicUsage(usage) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = m_vertexSize * m_vertexCount;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = IsDynamicUsage(usage) ? D3D11_CPU_ACCESS_WRITE : 0;
		bufferDesc.MiscFlags = 0;

		// Fill buffer with initial data on creation to speed things up
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem = initialData;
		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		ID3D11Device& D3DDevice = m_device;
		VERIFY(SUCCEEDED(D3DDevice.CreateBuffer(&bufferDesc, initialData == nullptr ? nullptr : &initData, &m_buffer)));
	}

	void * VertexBufferD3D11::Map(const LockOptions lock)
	{
		ID3D11DeviceContext& context = m_device;

		D3D11_MAPPED_SUBRESOURCE sub;

		if (lock == LockOptions::ReadOnly)
		{
			ASSERT(!m_tempStagingBuffer);

			// Create a staging buffer
			D3D11_BUFFER_DESC bufferDesc;
			ZeroMemory(&bufferDesc, sizeof(bufferDesc));
			bufferDesc.Usage = D3D11_USAGE_STAGING;
			bufferDesc.ByteWidth = m_vertexSize * m_vertexCount;
			bufferDesc.BindFlags = 0;
			bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
			bufferDesc.MiscFlags = 0;

			ID3D11Device& d3dDevice = m_device;
			VERIFY(SUCCEEDED(d3dDevice.CreateBuffer(&bufferDesc, nullptr, &m_tempStagingBuffer)));

			// Copy the resource from the hardware buffer over to the temp buffer
			context.CopyResource(m_tempStagingBuffer.Get(), m_buffer.Get());

			// Map the staging buffer instead
			VERIFY(SUCCEEDED(context.Map(m_tempStagingBuffer.Get(), 0, MapLockOptionsToD3D11(lock), 0, &sub)));
		}
		else
		{
			VERIFY(SUCCEEDED(context.Map(m_buffer.Get(), 0, MapLockOptionsToD3D11(lock), 0, &sub)));
		}

		return sub.pData;
	}

	void VertexBufferD3D11::Unmap()
	{
		ID3D11DeviceContext& context = m_device;

		if (m_tempStagingBuffer)
		{
			context.Unmap(m_tempStagingBuffer.Get(), 0);
			m_tempStagingBuffer.Reset();
		}
		else
		{
			context.Unmap(m_buffer.Get(), 0);
		}
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

		ID3D11DeviceContext& context = m_device;
		D3D11_MAPPED_SUBRESOURCE sub;
		VERIFY(SUCCEEDED(context.Map(m_buffer.Get(), 0, D3D11_MAP_READ, 0, &sub)));

		void* newBufferData = buffer->Map(LockOptions::Discard);
		memcpy(newBufferData, sub.pData, m_vertexSize * m_vertexCount);
		buffer->Unmap();

		context.Unmap(m_buffer.Get(), 0);

		return std::move(buffer);
	}
}
