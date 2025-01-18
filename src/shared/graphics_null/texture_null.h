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
		void Load(std::unique_ptr<std::istream>& stream) override;
		void LoadRaw(void* data, size_t dataSize) override;
		/// Gets the memory usage of this texture in bytes on the gpu.
		[[nodiscard]] uint32 GetMemorySize() const override;
		[[nodiscard]] void* GetTextureObject() const override { return nullptr; }
		[[nodiscard]] void* GetRawTexture() const override { return nullptr; }
		void Bind(ShaderType shader, uint32 slot = 0) override;
		void CopyPixelDataTo(uint8* destination) override;
		[[nodiscard]] uint32 GetPixelDataSize() const override;
		void UpdateFromMemory(void* data, size_t dataSize) override;

	private:
		GraphicsDeviceNull& m_device;
	};
}
