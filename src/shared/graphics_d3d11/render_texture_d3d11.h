// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/render_texture.h"
#include "render_target_d3d11.h"


namespace mmo
{
	class GraphicsDeviceD3D11;

	class RenderTextureD3D11 final
		: public RenderTexture
		, public RenderTargetD3D11
	{
	public:
		RenderTextureD3D11(GraphicsDeviceD3D11& device, std::string name, uint16 width, uint16 height, RenderTextureFlags flags, PixelFormat colorFormat = PixelFormat::R8G8B8A8, PixelFormat depthFormat = PixelFormat::D32F);
		~RenderTextureD3D11() override;


	public:
		virtual TexturePtr StoreToTexture() override;

	public:
		void LoadRaw(void* data, size_t dataSize) final override;
		void Bind(ShaderType shader, uint32 slot = 0) final override;
		void Activate() final override;
		void ApplyPendingResize() final override;
		void Clear(ClearFlags flags) final override;
		void Resize(uint16 width, uint16 height) final override;
		void Update() final override {};

		void* GetTextureObject() const final override;

		ID3D11Texture2D* GetTex2D() const { return m_renderTargetTex.Get(); }

		virtual void* GetRawTexture() const final override { return GetTex2D(); }

	public:
		inline ID3D11ShaderResourceView* GetColorShaderResourceView() const { ASSERT(HasColorBuffer() && HasShaderResourceView()); return m_colorShaderView.Get(); }

		inline ID3D11ShaderResourceView* GetDepthShaderResourceView() const { ASSERT(HasDepthBuffer() && HasShaderResourceView());  return m_depthShaderView.Get(); }

		/// @brief Gets the render target view.
		/// @return The render target view.
		inline ID3D11RenderTargetView* GetRenderTargetView() const { ASSERT(HasColorBuffer()); return m_renderTargetView.Get(); }

		/// @brief Gets the depth stencil view.
		/// @return The depth stencil view.
		inline ID3D11DepthStencilView* GetDepthStencilView() const { ASSERT(HasDepthBuffer()); return m_depthStencilView.Get(); }

	private:
		void CreateResources();

	public:
		void CopyPixelDataTo(uint8* destination) override;
		uint32 GetPixelDataSize() const override;
		void UpdateFromMemory(void* data, size_t dataSize) override;

	private:
		ComPtr<ID3D11Texture2D> m_renderTargetTex;
		ComPtr<ID3D11ShaderResourceView> m_colorShaderView;
		ComPtr<ID3D11ShaderResourceView> m_depthShaderView;
		bool m_resizePending;
	};

}
