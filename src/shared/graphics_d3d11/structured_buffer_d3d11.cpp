// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "structured_buffer_d3d11.h"

#include "base/macros.h"

namespace mmo
{
	StructuredBufferD3D11::StructuredBufferD3D11(
		ID3D11Device& device, 
		ID3D11DeviceContext& context, 
		const size_t elementSize, 
		const size_t elementCount, 
		const void* initialData)
		: StructuredBuffer(elementSize, elementCount)
		, m_device(device)
		, m_context(context)
	{
		// Create the buffer description
		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.ByteWidth = static_cast<UINT>(elementSize * elementCount);
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDesc.StructureByteStride = static_cast<UINT>(elementSize);

		// Set up initial data if provided
		D3D11_SUBRESOURCE_DATA subresourceData = {};
		D3D11_SUBRESOURCE_DATA* pInitialData = nullptr;
		if (initialData)
		{
			subresourceData.pSysMem = initialData;
			subresourceData.SysMemPitch = 0;
			subresourceData.SysMemSlicePitch = 0;
			pInitialData = &subresourceData;
		}

		// Create the buffer
		const HRESULT hr = m_device.CreateBuffer(&bufferDesc, pInitialData, m_buffer.GetAddressOf());
		ASSERT(SUCCEEDED(hr));

		// Create the shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(elementCount);

		const HRESULT srvHr = m_device.CreateShaderResourceView(m_buffer.Get(), &srvDesc, m_srv.GetAddressOf());
		ASSERT(SUCCEEDED(srvHr));
	}

	void StructuredBufferD3D11::BindToStage(const ShaderType type, const uint32 slot)
	{
		ID3D11ShaderResourceView* srvs[1] = { m_srv.Get() };

		switch (type)
		{
		case ShaderType::VertexShader:
			m_context.VSSetShaderResources(slot, 1, srvs);
			break;
		case ShaderType::PixelShader:
			m_context.PSSetShaderResources(slot, 1, srvs);
			break;
		default:
			ASSERT(!"Unsupported shader type for structured buffer binding");
			break;
		}
	}

	void StructuredBufferD3D11::Update(const void* data, const size_t elementCount)
	{
		ASSERT(elementCount <= m_elementCount);
		ASSERT(data != nullptr);

		// Map the buffer for writing
		D3D11_MAPPED_SUBRESOURCE mappedResource = {};
		const HRESULT hr = m_context.Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (SUCCEEDED(hr))
		{
			// Copy the data
			memcpy(mappedResource.pData, data, elementCount * m_elementSize);

			// Unmap the buffer
			m_context.Unmap(m_buffer.Get(), 0);
		}
	}
}
