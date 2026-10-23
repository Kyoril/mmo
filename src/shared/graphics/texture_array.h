// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "graphics/shader_base.h"

#include <memory>

namespace mmo
{
	/// @brief Texel formats a texture array can hold.
	enum class TextureArrayFormat
	{
		/// @brief Two unsigned normalized 8-bit channels (masks uploaded from the CPU).
		RG8,

		/// @brief Four unsigned normalized 8-bit channels (masks uploaded from the CPU).
		RGBA8,

		/// @brief One 32-bit float channel. The only float format compute shaders can both read and
		///        write through one unordered access view on every D3D11 device.
		R32F,

		/// @brief Four 16-bit float channels.
		RGBA16F,

		/// @brief Four 32-bit float channels.
		RGBA32F
	};

	/// @brief A 2D texture array. Readable from any shader stage; optionally writable from compute
	///        shaders and optionally carrying a mip chain that the GPU can rebuild from mip 0.
	class TextureArray : public NonCopyable
	{
	public:
		/// @brief Creates the description of a texture array.
		TextureArray(const uint16 width, const uint16 height, const uint16 layers, const TextureArrayFormat format, const bool writable, const bool mipmapped)
			: m_width(width)
			, m_height(height)
			, m_layers(layers)
			, m_format(format)
			, m_writable(writable)
			, m_mipmapped(mipmapped)
		{
		}

		~TextureArray() override = default;

	public:
		/// @brief Binds the whole array (all mips) for reading at a register of a shader stage.
		virtual void Bind(ShaderType stage, uint32 slot) = 0;

		/// @brief Clears a shader resource register of a stage, so the array can become a compute
		///        target again without the runtime having to resolve the hazard.
		virtual void Unbind(ShaderType stage, uint32 slot) = 0;

		/// @brief Binds mip 0 of every layer for writing at a compute shader UAV register. Only valid
		///        when writable.
		virtual void BindWritable(uint32 slot) = 0;

		/// @brief Uploads mip 0 of one layer. The size must equal width * height * bytes per texel.
		virtual void Upload(uint32 layer, const uint8* data, size_t size) = 0;

		/// @brief Rebuilds every lower mip of every layer from mip 0. No-op without a mip chain. The
		///        array must not be bound for writing while this runs.
		virtual void GenerateMips() = 0;

		/// @brief Width in texels.
		[[nodiscard]] uint16 GetWidth() const { return m_width; }

		/// @brief Height in texels.
		[[nodiscard]] uint16 GetHeight() const { return m_height; }

		/// @brief Number of layers.
		[[nodiscard]] uint16 GetLayerCount() const { return m_layers; }

		/// @brief Texel format.
		[[nodiscard]] TextureArrayFormat GetFormat() const { return m_format; }

		/// @brief Whether compute shaders may write the array.
		[[nodiscard]] bool IsWritable() const { return m_writable; }

		/// @brief Whether the array carries a full mip chain.
		[[nodiscard]] bool IsMipmapped() const { return m_mipmapped; }

	protected:
		uint16 m_width;
		uint16 m_height;
		uint16 m_layers;
		TextureArrayFormat m_format;
		bool m_writable;
		bool m_mipmapped;
	};

	using TextureArrayPtr = std::shared_ptr<TextureArray>;
}
