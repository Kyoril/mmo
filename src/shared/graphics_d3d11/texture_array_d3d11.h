// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics_device_d3d11.h"
#include "graphics/texture_array.h"

namespace mmo
{
	/// @brief Direct3D 11 texture array: an ID3D11Texture2D with ArraySize layers, a shader resource
	///        view over every mip, an unordered access view over mip 0 when writable, and the
	///        render-target binding GenerateMips needs when mipmapped.
	class TextureArrayD3D11 final : public TextureArray
	{
	public:
		TextureArrayD3D11(GraphicsDeviceD3D11& device, uint16 width, uint16 height, uint16 layers, TextureArrayFormat format, bool writable, bool mipmapped);
		~TextureArrayD3D11() override = default;

	public:
		void Bind(ShaderType stage, uint32 slot) override;

		void Unbind(ShaderType stage, uint32 slot) override;

		void BindWritable(uint32 slot) override;

		void Upload(uint32 layer, const uint8* data, size_t size) override;

		void GenerateMips() override;

		/// @brief Whether texture/SRV/(UAV) creation succeeded. GraphicsDeviceD3D11::CreateTextureArray
		///        checks this and returns nullptr instead of a half-constructed array on failure.
		[[nodiscard]] bool IsValid() const noexcept { return m_valid; }

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11Texture2D> m_texture;
		ComPtr<ID3D11ShaderResourceView> m_shaderView;
		ComPtr<ID3D11UnorderedAccessView> m_unorderedView;
		uint32 m_mipLevels = 1;
		bool m_valid = false;
	};
}
