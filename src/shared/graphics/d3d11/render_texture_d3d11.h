// Copyright (C) 2020, Robin Klimonow. All rights reserved.

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
		virtual ~RenderTextureD3D11() = default;

	public:
		virtual void LoadRaw(void* data, size_t dataSize) final override;
		virtual void Bind(ShaderType shader, uint32 slot = 0) final override;
		virtual void Activate() final override;
		virtual void Clear(ClearFlags flags) final override;
		virtual void Resize(uint16 width, uint16 height) final override;
		virtual void Update() final override {};

	public:
		inline ID3D11ShaderResourceView* GetShaderResourceView() const { return m_shaderResourceView.Get(); }

	private:
		void CreateResources();

	private:
		ComPtr<ID3D11Texture2D> m_renderTargetTex;
		ComPtr<ID3D11ShaderResourceView> m_shaderResourceView;
		bool m_resizePending;
	};

}
