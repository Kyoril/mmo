// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "graphics/texture.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	/// D3D11 implementation of the texture class.
	class TextureD3D11 final
		: public Texture
	{
	public:
		/// Initializes a new instance of the TextureD3D11 class.
		TextureD3D11(GraphicsDeviceD3D11& device, uint16 width, uint16 height);

	public:
		virtual void Load(std::unique_ptr<std::istream>& stream) final override;
		virtual void LoadRaw(void* data, size_t dataSize) final override;
		/// Gets the memory usage of this texture in bytes on the gpu.
		virtual uint32 GetMemorySize() const;
		virtual void* GetTextureObject() const final override { return m_shaderView.Get(); }

	private:
		void CreateShaderResourceView();

	public:
		virtual void Bind(ShaderType shader, uint32 slot = 0) final override;

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11Texture2D> m_texture;
		ComPtr<ID3D11ShaderResourceView> m_shaderView;
	};
}
