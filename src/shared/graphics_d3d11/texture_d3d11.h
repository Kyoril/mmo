// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/texture.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	class RenderTextureD3D11;
}

namespace mmo
{
	/// D3D11 implementation of the texture class.
	class TextureD3D11 final
		: public Texture
	{
	public:
		/// Initializes a new instance of the TextureD3D11 class.
		TextureD3D11(GraphicsDeviceD3D11& device, uint16 width, uint16 height, BufferUsage usage);

	public:
		void FromRenderTexture(RenderTextureD3D11& renderTexture);

		void Load(std::unique_ptr<std::istream>& stream) override;

		void LoadRaw(void* data, size_t dataSize) override;

		void UpdateFromMemory(void* data, size_t dataSize) override;

		/// Gets the memory usage of this texture in bytes on the gpu.
		[[nodiscard]] uint32 GetMemorySize() const override;

		[[nodiscard]] void* GetTextureObject() const override { return m_shaderView.Get(); }

		[[nodiscard]] void* GetRawTexture() const override { return m_texture.Get(); }

		void CopyPixelDataTo(uint8* destination) override;

		[[nodiscard]] uint32 GetPixelDataSize() const override;

	private:
		void CreateShaderResourceView();

	public:
		void Bind(ShaderType shader, uint32 slot = 0) final override;

	private:
		GraphicsDeviceD3D11& m_device;
		BufferUsage m_usage;
		ComPtr<ID3D11Texture2D> m_texture;
		ComPtr<ID3D11ShaderResourceView> m_shaderView;
	};
}
