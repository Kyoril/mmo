// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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


	IndexBufferD3D11::IndexBufferD3D11(GraphicsDeviceD3D11 & InDevice, size_t IndexCount, IndexBufferSize IndexSize, const void* InitialData)
		: IndexBuffer(IndexCount, IndexSize)
		, Device(InDevice)
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
		ID3D11Device& D3DDevice = Device;
		VERIFY(SUCCEEDED(D3DDevice.CreateBuffer(&BufferDesc, InitialData == nullptr ? nullptr : &InitData, &Buffer)));
	}

	void * IndexBufferD3D11::Map()
	{
		ID3D11DeviceContext& Context = Device;

		D3D11_MAPPED_SUBRESOURCE Sub;
		VERIFY(SUCCEEDED(Context.Map(Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Sub)));

		return Sub.pData;
	}

	void IndexBufferD3D11::Unmap()
	{
		ID3D11DeviceContext& Context = Device;
		Context.Unmap(Buffer.Get(), 0);
	}

	void IndexBufferD3D11::Set()
	{
		ID3D11DeviceContext& Context = Device;
		Context.IASetIndexBuffer(Buffer.Get(), IndexBufferFormat(IndexSize), 0);

		Device.SetIndexCount(static_cast<UINT>(IndexCount));
	}
}
