// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "render_texture_d3d11.h"
#include "graphics_device_d3d11.h"

#include "base/macros.h"

#include <d3d11.h>

namespace mmo
{
	RenderTextureD3D11::RenderTextureD3D11(GraphicsDeviceD3D11 & device, std::string name, uint16 width, uint16 height)
		: RenderTexture(std::move(name), width, height)
		, RenderTargetD3D11(device)
		, m_resizePending(false)
	{
		CreateResources();
	}

	void RenderTextureD3D11::LoadRaw(void * data, size_t dataSize)
	{
		throw std::runtime_error("Method not implemented");
	}

	void RenderTextureD3D11::Bind(ShaderType shader, uint32 slot)
	{
		ID3D11DeviceContext& context = m_device;

		ID3D11ShaderResourceView* const views = m_shaderResourceView.Get();
		switch (shader)
		{
		case ShaderType::VertexShader:
			context.VSSetShaderResources(slot, 1, &views);
			break;
		case ShaderType::PixelShader:
			context.PSSetShaderResources(slot, 1, &views);
			break;
		}
	}

	void RenderTextureD3D11::Activate()
	{
		if (m_resizePending)
		{
			m_resizePending = false;

			// Reset resources
			m_shaderResourceView.Reset();
			m_renderTargetView.Reset();
			m_renderTargetTex.Reset();

			// Recreate resources with new dimensions
			CreateResources();
		}

		RenderTarget::Activate();
		RenderTargetD3D11::Activate();

		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderTextureD3D11::Clear(ClearFlags flags)
	{
		RenderTargetD3D11::Clear(flags);
	}

	void RenderTextureD3D11::Resize(uint16 width, uint16 height)
	{
		m_width = width;
		m_height = height;
		m_resizePending = true;
	}

	void RenderTextureD3D11::CreateResources()
	{
		// Obtain the d3d11 device object
		ID3D11Device& d3dDev = m_device;

		// Initialize the render target texture description.
		D3D11_TEXTURE2D_DESC textureDesc;
		ZeroMemory(&textureDesc, sizeof(textureDesc));
		textureDesc.Width = m_width;
		textureDesc.Height = m_height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// TODO: expose format
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		textureDesc.CPUAccessFlags = 0;
		textureDesc.MiscFlags = 0;

		// Create the render target texture.
		VERIFY(SUCCEEDED(d3dDev.CreateTexture2D(&textureDesc, nullptr, &m_renderTargetTex)));

		// Setup the description of the render target view.
		D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
		renderTargetViewDesc.Format = textureDesc.Format;
		renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		renderTargetViewDesc.Texture2D.MipSlice = 0;
		VERIFY(SUCCEEDED(d3dDev.CreateRenderTargetView(m_renderTargetTex.Get(), &renderTargetViewDesc, &m_renderTargetView)));

		// Setup the description of the shader resource view.
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
		shaderResourceViewDesc.Format = textureDesc.Format;
		shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
		shaderResourceViewDesc.Texture2D.MipLevels = 1;
		d3dDev.CreateShaderResourceView(m_renderTargetTex.Get(), &shaderResourceViewDesc, &m_shaderResourceView);

		// Create a depth buffer
		D3D11_TEXTURE2D_DESC texd;
		ZeroMemory(&texd, sizeof(texd));
		texd.Width = m_width;
		texd.Height = m_height;
		texd.ArraySize = 1;
		texd.MipLevels = 1;
		texd.SampleDesc.Count = 1;
		texd.Format = DXGI_FORMAT_D32_FLOAT;
		texd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		ComPtr<ID3D11Texture2D> depthBuffer;
		VERIFY(SUCCEEDED(d3dDev.CreateTexture2D(&texd, nullptr, &depthBuffer)));

		// Create the depth stencil view
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
		ZeroMemory(&dsvd, sizeof(dsvd));
		dsvd.Format = DXGI_FORMAT_D32_FLOAT;
		dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
		VERIFY(SUCCEEDED(d3dDev.CreateDepthStencilView(depthBuffer.Get(), &dsvd, &m_depthStencilView)));
	}
}
