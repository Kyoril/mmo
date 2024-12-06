// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "render_target_metal.h"
#include "graphics/render_texture.h"


namespace mmo
{
	class GraphicsDeviceMetal;

	class RenderTextureMetal final
		: public RenderTexture
		, public RenderTargetMetal
	{
	public:
		RenderTextureMetal(GraphicsDeviceMetal& device, std::string name, uint16 width, uint16 height);
		virtual ~RenderTextureMetal() = default;

	public:
		virtual void LoadRaw(void* data, size_t dataSize) final override;
		virtual void Bind(ShaderType shader, uint32 slot = 0) final override;
		virtual void Activate() final override;
		virtual void Clear(ClearFlags flags) final override;
		virtual void Resize(uint16 width, uint16 height) final override;
		virtual void Update() final override {};
		virtual void* GetTextureObject() const final override { return nullptr; }
        
        TexturePtr StoreToTexture() override;
        
        void* GetRawTexture() const override;

        void CopyPixelDataTo(uint8* destination) override;

        uint32 GetPixelDataSize() const override;
        
	private:
		bool m_resizePending;
	};

}
