// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/texture.h"
#include "graphics_device_null.h"


namespace mmo
{
	/// D3D11 implementation of the texture class.
	class TextureNull final
		: public Texture
	{
	public:
		/// Initializes a new instance of the TextureD3D11 class.
		TextureNull(GraphicsDeviceNull& device, uint16 width, uint16 height);

	public:
		virtual void Load(std::unique_ptr<std::istream>& stream) final override;
		virtual void LoadRaw(void* data, size_t dataSize) final override;
		/// Gets the memory usage of this texture in bytes on the gpu.
		virtual uint32 GetMemorySize() const override;
		virtual void* GetTextureObject() const final override { return nullptr; }
		virtual void* GetRawTexture() const final override { return nullptr; }
		virtual void Bind(ShaderType shader, uint32 slot = 0) final override;
		void CopyPixelDataTo(uint8* destination) override;
		uint32 GetPixelDataSize() const override;

	private:
		GraphicsDeviceNull& m_device;
	};
}
