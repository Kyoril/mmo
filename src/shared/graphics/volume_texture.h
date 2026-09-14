// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "graphics/shader_base.h"

#include <memory>

namespace mmo
{
	/// @brief Texel formats a volume texture can hold.
	enum class VolumeFormat
	{
		/// @brief One unsigned normalized 8-bit channel (noise).
		R8,

		/// @brief Four 16-bit float channels (fog scattering and extinction).
		RGBA16F
	};

	/// @brief A 3D texture. Readable from any shader stage; optionally writable from compute shaders.
	class VolumeTexture : public NonCopyable
	{
	public:
		/// @brief Creates the description of a volume texture.
		VolumeTexture(const uint16 width, const uint16 height, const uint16 depth, const VolumeFormat format, const bool writable)
			: m_width(width)
			, m_height(height)
			, m_depth(depth)
			, m_format(format)
			, m_writable(writable)
		{
		}

		~VolumeTexture() override = default;

	public:
		/// @brief Binds the texture for reading at a register of a shader stage.
		virtual void Bind(ShaderType stage, uint32 slot) = 0;

		/// @brief Binds the texture for writing at a compute shader UAV register. Only valid when writable.
		virtual void BindWritable(uint32 slot) = 0;

		/// @brief Uploads texel data. The size must equal width * height * depth * bytes per texel.
		virtual void Upload(const uint8* data, size_t size) = 0;

		/// @brief Width in texels.
		[[nodiscard]] uint16 GetWidth() const { return m_width; }

		/// @brief Height in texels.
		[[nodiscard]] uint16 GetHeight() const { return m_height; }

		/// @brief Depth in texels.
		[[nodiscard]] uint16 GetDepth() const { return m_depth; }

		/// @brief Texel format.
		[[nodiscard]] VolumeFormat GetFormat() const { return m_format; }

		/// @brief Whether compute shaders may write the texture.
		[[nodiscard]] bool IsWritable() const { return m_writable; }

	protected:
		uint16 m_width;
		uint16 m_height;
		uint16 m_depth;
		VolumeFormat m_format;
		bool m_writable;
	};

	using VolumeTexturePtr = std::shared_ptr<VolumeTexture>;
}
