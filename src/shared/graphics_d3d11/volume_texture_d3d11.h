// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics_device_d3d11.h"
#include "graphics/volume_texture.h"

namespace mmo
{
	/// @brief Direct3D 11 volume texture: an ID3D11Texture3D with a shader resource view and, when
	///        writable, an unordered access view.
	class VolumeTextureD3D11 final : public VolumeTexture
	{
	public:
		VolumeTextureD3D11(GraphicsDeviceD3D11& device, uint16 width, uint16 height, uint16 depth, VolumeFormat format, bool writable);
		~VolumeTextureD3D11() override = default;

	public:
		void Bind(ShaderType stage, uint32 slot) override;

		void BindWritable(uint32 slot) override;

		void Upload(const uint8* data, size_t size) override;

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11Texture3D> m_texture;
		ComPtr<ID3D11ShaderResourceView> m_shaderView;
		ComPtr<ID3D11UnorderedAccessView> m_unorderedView;
	};
}
