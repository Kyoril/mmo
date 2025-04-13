// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_texture_null.h"
#include "graphics_device_null.h"

#include "base/macros.h"

namespace mmo
{
RenderTextureNull::RenderTextureNull(GraphicsDeviceNull & device, std::string name, uint16 width, uint16 height, PixelFormat format)
	: RenderTexture(std::move(name), width, height, RenderTextureFlags::None, R8G8B8A8, D32F)
		, RenderTargetNull(device)
		, m_resizePending(false)
	{
		ASSERT(width > 0);
		ASSERT(height > 0);
	}

	void RenderTextureNull::LoadRaw(void * data, size_t dataSize)
	{
		throw std::runtime_error("Method not implemented");
	}

	void RenderTextureNull::Bind(ShaderType shader, uint32 slot)
	{
	}

	void RenderTextureNull::Activate()
	{
		if (m_resizePending)
		{
			m_resizePending = false;
		}

		RenderTarget::Activate();
		RenderTargetNull::Activate();

		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderTextureNull::Clear(ClearFlags flags)
	{
		RenderTargetNull::Clear(flags);
	}

	void RenderTextureNull::Resize(uint16 width, uint16 height)
	{
		m_width = width;
		m_height = height;
		m_resizePending = true;
	}

	TexturePtr RenderTextureNull::StoreToTexture()
	{
		return nullptr;
	}

	void RenderTextureNull::CopyPixelDataTo(uint8* destination)
	{
	}

	uint32 RenderTextureNull::GetPixelDataSize() const
	{
		return 0;
	}

	void RenderTextureNull::UpdateFromMemory(void* data, size_t dataSize)
	{
		
	}
}
