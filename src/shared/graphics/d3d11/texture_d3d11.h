// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/texture.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	/// D3D11 implementation of the texture class.
	class TextureD3D11
		: public Texture
	{
	public:
		/// Initializes a new instance of the TextureD3D11 class.
		TextureD3D11(GraphicsDeviceD3D11& device);

	public:
		virtual void Load(std::unique_ptr<std::istream>& stream) override;

	public:

		void Bind(ShaderType shader, uint32 slot);

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11Texture2D> m_texture;
		ComPtr<ID3D11ShaderResourceView> m_shaderView;
	};
}