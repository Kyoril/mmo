
#pragma once

#include "graphics/render_texture.h"

#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace mmo
{
	class GraphicsDeviceD3D11;

	class RenderTextureD3D11 final
		: public RenderTexture
	{
	public:
		RenderTextureD3D11(GraphicsDeviceD3D11& device, std::string name, uint16 width, uint16 height);
		virtual ~RenderTextureD3D11() = default;

	public:
		virtual void LoadRaw(void* data, size_t dataSize) final override;
		virtual void Bind(ShaderType shader, uint32 slot = 0) final override;
		virtual void Activate() final override;

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11Texture2D> m_renderTargetTex;
		ComPtr<ID3D11RenderTargetView> m_renderTargetView;
		ComPtr<ID3D11ShaderResourceView> m_shaderResourceView;
	};

}
