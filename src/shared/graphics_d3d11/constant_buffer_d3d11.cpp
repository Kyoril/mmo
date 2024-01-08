
#include "constant_buffer_d3d11.h"

namespace mmo
{
	ConstantBufferD3D11::ConstantBufferD3D11(ID3D11Device& device, ID3D11DeviceContext& context, size_t size, const void* initialData)
		: ConstantBuffer(size)
		, m_device(device)
		, m_context(context)
	{
		D3D11_BUFFER_DESC desc;
		desc.ByteWidth = static_cast<uint32>(size);
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
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

	void ConstantBufferD3D11::Update(void* data)
	{
		m_context.UpdateSubresource(m_buffer.Get(), 0, nullptr, data, 0, 0);
	}
}
