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
		RenderTextureD3D11(GraphicsDeviceD3D11& device, std::string name, uint16 width, uint16 height);
		~RenderTextureD3D11() override;


	public:
		virtual TexturePtr StoreToTexture() override;

	public:
		void LoadRaw(void* data, size_t dataSize) final override;
		void Bind(ShaderType shader, uint32 slot = 0) final override;
		void Activate() final override;
		void Clear(ClearFlags flags) final override;
		void Resize(uint16 width, uint16 height) final override;
		void Update() final override {};

		void* GetTextureObject() const final override { return m_shaderResourceView.Get(); }

		ID3D11Texture2D* GetTex2D() const { return m_renderTargetTex.Get(); }

		virtual void* GetRawTexture() const final override { return GetTex2D(); }

	public:
		inline ID3D11ShaderResourceView* GetShaderResourceView() const { return m_shaderResourceView.Get(); }

	private:
		void CreateResources();

	public:
		void CopyPixelDataTo(uint8* destination) override;
		uint32 GetPixelDataSize() const override;
		void UpdateFromMemory(void* data, size_t dataSize) override;

	private:
		ComPtr<ID3D11Texture2D> m_renderTargetTex;
		ComPtr<ID3D11ShaderResourceView> m_shaderResourceView;
		bool m_resizePending;
	};

}
