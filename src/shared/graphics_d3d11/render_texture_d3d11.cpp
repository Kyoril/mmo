// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_texture_d3d11.h"
#include "graphics_device_d3d11.h"

#include "base/macros.h"

#include <d3d11.h>

#include "texture_d3d11.h"
#include "graphics/texture_mgr.h"

namespace mmo
{
	RenderTextureD3D11::RenderTextureD3D11(GraphicsDeviceD3D11 & device, std::string name, uint16 width, uint16 height, RenderTextureFlags flags, PixelFormat colorFormat, PixelFormat depthFormat)
		: RenderTexture(std::move(name), width, height, flags, colorFormat, depthFormat)
		, RenderTargetD3D11(device)
		, m_resizePending(false)
	{
		ASSERT(width > 0);
		ASSERT(height > 0);

		// BULLSHIT
		m_header.width = width;
		m_header.height = height;

		CreateResources();
	}

	RenderTextureD3D11::~RenderTextureD3D11()
	{
	}

	TexturePtr RenderTextureD3D11::StoreToTexture()
	{
		auto texture = std::make_shared<TextureD3D11>(m_device, m_width, m_height, BufferUsage::Static);
		texture->FromRenderTexture(*this);

		return texture;
	}

	void RenderTextureD3D11::LoadRaw(void * data, size_t dataSize)
	{
		throw std::runtime_error("Method not implemented");
	}

	void RenderTextureD3D11::Bind(ShaderType shader, uint32 slot)
	{
		ASSERT(HasShaderResourceView());

		ID3D11DeviceContext& context = m_device;

		ID3D11ShaderResourceView* views;
		if (HasColorBuffer())
		{
			views = m_colorShaderView.Get();
		}
		else
		{
			views = m_depthShaderView.Get();
		}

		switch (shader)
		{
		case ShaderType::VertexShader:
			context.VSSetShaderResources(slot, 1, &views);
			break;
		case ShaderType::PixelShader:
			context.PSSetShaderResources(slot, 1, &views);
			break;
		default:
			throw std::runtime_error("Shader type not yet supported for binding!");
		}
	}

	void RenderTextureD3D11::Activate()
	{
		ApplyPendingResize();

		RenderTarget::Activate();
		RenderTargetD3D11::Activate();

		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderTextureD3D11::ApplyPendingResize()
	{
		if (m_resizePending)
		{
			m_resizePending = false;

			// Reset resources
			m_depthStencilView.Reset();
			m_colorShaderView.Reset();
			m_renderTargetView.Reset();
			m_renderTargetTex.Reset();

			// Recreate resources with new dimensions
			CreateResources();
		}
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

	void* RenderTextureD3D11::GetTextureObject() const
	{
		ASSERT(HasShaderResourceView());

		return HasColorBuffer() ? m_colorShaderView.Get() : m_depthShaderView.Get();
	}

	void RenderTextureD3D11::CreateResources()
	{
		// Obtain the d3d11 device object
		ID3D11Device& d3d_dev = m_device;

		if (HasColorBuffer())
		{
			// Map pixel format to DXGI format
			DXGI_FORMAT dxgiColorFormat;
			switch (m_colorFormat)
			{
			case PixelFormat::R8G8B8A8:
				dxgiColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
				break;
			case PixelFormat::B8G8R8A8:
				dxgiColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
				break;
			case PixelFormat::R16G16B16A16:
				dxgiColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
				break;
			case PixelFormat::R32G32B32A32:
				dxgiColorFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
				break;
			case D32F:
				dxgiColorFormat = DXGI_FORMAT_D32_FLOAT;
				break;
			default:
				dxgiColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
				break;
			}

			// Initialize the render target texture description.
			D3D11_TEXTURE2D_DESC textureDesc;
			ZeroMemory(&textureDesc, sizeof(textureDesc));
			textureDesc.Width = m_width;
			textureDesc.Height = m_height;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = dxgiColorFormat;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
			if (HasShaderResourceView())
			{
				textureDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
			}
			textureDesc.CPUAccessFlags = 0;
			textureDesc.MiscFlags = 0;

			// Create the render target texture.
			VERIFY(SUCCEEDED(d3d_dev.CreateTexture2D(&textureDesc, nullptr, &m_renderTargetTex)));

			// Setup the description of the render target view.
			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
			renderTargetViewDesc.Format = textureDesc.Format;
			renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
			renderTargetViewDesc.Texture2D.MipSlice = 0;
			VERIFY(SUCCEEDED(d3d_dev.CreateRenderTargetView(m_renderTargetTex.Get(), &renderTargetViewDesc, &m_renderTargetView)));

			if (HasShaderResourceView())
			{
				// Setup the description of the shader resource view.
				D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
				shaderResourceViewDesc.Format = textureDesc.Format;
				shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
				shaderResourceViewDesc.Texture2D.MipLevels = 1;
				d3d_dev.CreateShaderResourceView(m_renderTargetTex.Get(), &shaderResourceViewDesc, &m_colorShaderView);
			}
		}

		if (HasDepthBuffer())
		{
			// Map pixel format to DXGI format
			DXGI_FORMAT dxgiDepthBufferFormat;
			DXGI_FORMAT dxgiDepthBufferViewFormat;
			switch (m_depthFormat)
			{
			case D32F:
				dxgiDepthBufferFormat = DXGI_FORMAT_R32_TYPELESS;
				dxgiDepthBufferViewFormat = DXGI_FORMAT_D32_FLOAT;
				break;
			default:
				ASSERT(!"Unsupported depth buffer format!");
				break;
			}

			// Create a depth buffer
			D3D11_TEXTURE2D_DESC texd;
			ZeroMemory(&texd, sizeof(texd));
			texd.Width = m_width;
			texd.Height = m_height;
			texd.ArraySize = 1;
			texd.MipLevels = 1;
			texd.SampleDesc.Count = 1;
			texd.Format = DXGI_FORMAT_R24G8_TYPELESS;
			texd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			if (HasShaderResourceView())
			{
				texd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
			}
			ComPtr<ID3D11Texture2D> depthBuffer;
			VERIFY(SUCCEEDED(d3d_dev.CreateTexture2D(&texd, nullptr, &depthBuffer)));

			// Create the depth stencil view
			D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
			ZeroMemory(&dsvd, sizeof(dsvd));
			dsvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			VERIFY(SUCCEEDED(d3d_dev.CreateDepthStencilView(depthBuffer.Get(), &dsvd, &m_depthStencilView)));

			if (HasShaderResourceView())
			{
				// Setup the description of the shader resource view.
				D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
				shaderResourceViewDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
				shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
				shaderResourceViewDesc.Texture2D.MipLevels = 1;
				d3d_dev.CreateShaderResourceView(depthBuffer.Get(), &shaderResourceViewDesc, &m_depthShaderView);
			}
		}
	}

	void RenderTextureD3D11::CopyPixelDataTo(uint8* destination)
	{
		// Step 1: Create a staging texture with CPU read access
		D3D11_TEXTURE2D_DESC textureDesc;
		m_renderTargetTex->GetDesc(&textureDesc);

		textureDesc.Usage = D3D11_USAGE_STAGING;
		textureDesc.BindFlags = 0;
		textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		ID3D11Texture2D* stagingTexture = nullptr;
		ID3D11Device& d3d11Device = m_device;
		HRESULT hr = d3d11Device.CreateTexture2D(&textureDesc, nullptr, &stagingTexture);
		if (FAILED(hr)) return;

		// Step 2: Copy the texture data to the staging texture
		ID3D11DeviceContext* context = nullptr;
		d3d11Device.GetImmediateContext(&context);
		context->CopyResource(stagingTexture, m_renderTargetTex.Get());

		// Step 3: Map the staging texture to read its data
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
		if (FAILED(hr))
		{
			stagingTexture->Release();
			context->Release();
			return;
		}

		// Copy pixel data to buffer
		memcpy(destination, mappedResource.pData, GetPixelDataSize());

		context->Unmap(stagingTexture, 0);
		stagingTexture->Release();
		context->Release();
	}

	uint32 RenderTextureD3D11::GetPixelDataSize() const
	{
		return m_width * m_height * 4;
	}

	void RenderTextureD3D11::UpdateFromMemory(void* data, size_t dataSize)
	{
		UNREACHABLE();
	}
}
