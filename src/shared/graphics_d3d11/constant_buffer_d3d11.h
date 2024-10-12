#pragma once

#include "graphics_device_d3d11.h"
#include "graphics/constant_buffer.h"

namespace mmo
{
	class ConstantBufferD3D11 final : public ConstantBuffer
	{
	public:
		explicit ConstantBufferD3D11(ID3D11Device& device, ID3D11DeviceContext& context, size_t size, const void* initialData);

		~ConstantBufferD3D11() override;

	public:
		void BindToStage(ShaderType type, uint32 slot) override;

		void Update(void* data) override;

		ID3D11Buffer* GetBuffer() const { return m_buffer.Get(); }

	protected:
		ID3D11Device& m_device;
		ID3D11DeviceContext& m_context;
		ComPtr<ID3D11Buffer> m_buffer;
	};
}