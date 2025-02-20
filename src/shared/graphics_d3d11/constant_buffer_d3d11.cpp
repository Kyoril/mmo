
#include "constant_buffer_d3d11.h"

namespace mmo
{
	ConstantBufferD3D11::ConstantBufferD3D11(ID3D11Device& device, ID3D11DeviceContext& context, size_t size, const void* initialData)
		: ConstantBuffer(size)
		, m_device(device)
		, m_context(context)
	{
		// Ensure size aligns to 16
		if (size % 16 != 0)
		{
			size = ((size / 16) * 16) + 16;
		}

		D3D11_BUFFER_DESC desc;
		desc.ByteWidth = static_cast<uint32>(size);
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.StructureByteStride = 0;

		D3D11_SUBRESOURCE_DATA data;
		data.SysMemPitch = 0;
		data.SysMemSlicePitch = 0;
		data.pSysMem = initialData;

		VERIFY(SUCCEEDED(device.CreateBuffer(&desc, initialData != nullptr ? &data : nullptr, &m_buffer)));
	}

	ConstantBufferD3D11::~ConstantBufferD3D11()
	{
	}

	void ConstantBufferD3D11::BindToStage(ShaderType type, uint32 slot)
	{
		ID3D11Buffer* buffers[1] = { m_buffer.Get() };

		switch(type)
		{
		case ShaderType::VertexShader:
			m_context.VSSetConstantBuffers(slot, 1, buffers);
			break;

		case ShaderType::PixelShader:
			m_context.PSSetConstantBuffers(slot, 1, buffers);
			break;

		case ShaderType::GeometryShader:
			m_context.GSSetConstantBuffers(slot, 1, buffers);
			break;

		case ShaderType::DomainShader:
			m_context.DSSetConstantBuffers(slot, 1, buffers);
			break;

		case ShaderType::HullShader:
			m_context.HSSetConstantBuffers(slot, 1, buffers);
			break;

		case ShaderType::ComputeShader:
			m_context.CSSetConstantBuffers(slot, 1, buffers);
			break;
		}
		
	}

	void ConstantBufferD3D11::Update(void* data)
	{
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = m_context.Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr))
        {
            memcpy(mappedResource.pData, data, m_size);
            m_context.Unmap(m_buffer.Get(), 0);
        }
        else
        {
            // Handle the error (e.g., log it)
        }

		//m_context.UpdateSubresource(m_buffer.Get(), 0, nullptr, data, 0, 0);
	}
}
