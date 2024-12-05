// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "render_texture_metal.h"
#include "graphics_device_metal.h"

#include "base/macros.h"

namespace mmo
{
    RenderTextureMetal::RenderTextureMetal(GraphicsDeviceMetal & device, std::string name, uint16 width, uint16 height)
		: RenderTexture(std::move(name), width, height)
		, RenderTargetMetal(device)
		, m_resizePending(false)
	{
		ASSERT(width > 0);
		ASSERT(height > 0);
	}

	void RenderTextureMetal::LoadRaw(void * data, size_t dataSize)
	{
		throw std::runtime_error("Method not implemented");
	}

	void RenderTextureMetal::Bind(ShaderType shader, uint32 slot)
	{
	}

	void RenderTextureMetal::Activate()
	{
		if (m_resizePending)
		{
			m_resizePending = false;
		}

		RenderTarget::Activate();
		RenderTargetMetal::Activate();

		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderTextureMetal::Clear(ClearFlags flags)
	{
		RenderTargetMetal::Clear(flags);
	}

	void RenderTextureMetal::Resize(uint16 width, uint16 height)
	{
		m_width = width;
		m_height = height;
		m_resizePending = true;
	}

    TexturePtr RenderTextureMetal::StoreToTexture()
    {
        return nullptr;
    }

    void* RenderTextureMetal::GetRawTexture() const
    {
        return nullptr;
    }

    void RenderTextureMetal::CopyPixelDataTo(uint8* destination)
    {
        
    }

    uint32 RenderTextureMetal::GetPixelDataSize() const
    {
        return 0;
    }
}
