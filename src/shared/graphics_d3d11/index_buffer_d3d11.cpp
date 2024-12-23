// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "index_buffer_d3d11.h"

#include "base/macros.h"


namespace mmo
{
	namespace
	{
		/// Determines the DXGI_FORMAT value from an EGxIndexBufferSize value.
		static inline DXGI_FORMAT IndexBufferFormat(IndexBufferSize Size)
		{
			switch (Size)
			{
			case IndexBufferSize::Index_16:
				return DXGI_FORMAT_R16_UINT;
			case IndexBufferSize::Index_32:
				return DXGI_FORMAT_R32_UINT;
			}

			ASSERT(false);
			return DXGI_FORMAT_UNKNOWN;
		}

		/// Gets the size of an index value in bytes based on an EGxIndexBufferSize value.
		static inline uint32 IndexSizeInBytes(IndexBufferSize Size)
		{
			switch (Size)
			{
			case IndexBufferSize::Index_16:
				return sizeof(uint16);
			case IndexBufferSize::Index_32:
				return sizeof(uint32);
			}

			ASSERT(false);
			return 0;
		}
	}


	IndexBufferD3D11::IndexBufferD3D11(GraphicsDeviceD3D11 & InDevice, size_t IndexCount, IndexBufferSize IndexSize, BufferUsage usage, const void* InitialData)
		: IndexBuffer(IndexCount, IndexSize)
		, m_device(InDevice)
	{
		// Create an index buffer
		D3D11_BUFFER_DESC BufferDesc;
		ZeroMemory(&BufferDesc, sizeof(BufferDesc));
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.ByteWidth = IndexSizeInBytes(IndexSize) * static_cast<UINT>(IndexCount);
		BufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufferDesc.MiscFlags = 0;

		// Fill initial data structure
		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = InitialData;
		InitData.SysMemPitch = 0;
		InitData.SysMemSlicePitch = 0;

		// Create the index buffer
		ID3D11Device& D3DDevice = m_device;
		VERIFY(SUCCEEDED(D3DDevice.CreateBuffer(&BufferDesc, InitialData == nullptr ? nullptr : &InitData, &m_buffer)));
	}

	void * IndexBufferD3D11::Map(LockOptions lock)
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
			bufferDesc.ByteWidth = IndexSizeInBytes(IndexSize) * static_cast<UINT>(IndexCount);
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

	void IndexBufferD3D11::Unmap()
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

	void IndexBufferD3D11::Set(uint16 slot)
	{
		ID3D11DeviceContext& Context = m_device;
		Context.IASetIndexBuffer(m_buffer.Get(), IndexBufferFormat(IndexSize), 0);

		m_device.SetIndexCount(static_cast<UINT>(IndexCount));
	}
}
