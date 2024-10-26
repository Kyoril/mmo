// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "render_target_null.h"
#include "graphics/render_texture.h"


namespace mmo
{
	class GraphicsDeviceD3D11;

	class RenderTextureNull final
		: public RenderTexture
		, public RenderTargetNull
	{
	public:
		RenderTextureNull(GraphicsDeviceNull& device, std::string name, uint16 width, uint16 height);
		virtual ~RenderTextureNull() = default;

	public:
		virtual void LoadRaw(void* data, size_t dataSize) final override;
		virtual void Bind(ShaderType shader, uint32 slot = 0) final override;
		virtual void Activate() final override;
		virtual void Clear(ClearFlags flags) final override;
		virtual void Resize(uint16 width, uint16 height) final override;
		virtual void Update() final override {};
		virtual void* GetTextureObject() const final override { return nullptr; }
		virtual void* GetRawTexture() const final override { return nullptr; }
		TexturePtr StoreToTexture() override;
		void CopyPixelDataTo(uint8* destination) override;
		uint32 GetPixelDataSize() const override;

	private:
		bool m_resizePending;
	};

}
