// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shader_base.h"

#include "tex_v1_0/header.h"
#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header_load.h"

#include "binary_io/stream_source.h"
#include "binary_io/reader.h"

#include "base/macros.h"

namespace mmo
{
	enum PixelFormat : uint8
	{
		R8G8B8A8,
		B8G8R8A8,

		R16G16B16A16,
		R32G32B32A32,

		DXT1,
		DXT3,
		DXT5,

		D32F,

		Unknown
	};

	/// Enumerates possible face cull modes.
	enum class TextureAddressMode
	{
		/// Coordinates are clamped if exceeding the range of 0..1.
		Clamp,

		/// Coordinates are wrapped if exceeding the range of 0..1.
		Wrap,

		/// Coordinates are mirrored if exceeding the range of 0..1.
		Mirror,

		/// Anything outside of the range of 0..1 is rendered using a border color.
		Border,
	};

	/// Enumerates possible face cull modes.
	enum class TextureFilter
	{
		/// No texture filtering.
		None,

		/// Bilinear filter.
		Bilinear,

		/// Trilinear filter.
		Trilinear,

		/// Anisotropic filter
		Anisotropic,
	};
	
	/// This is the base class of a texture.
	class Texture : public NonCopyable
	{
	public:
		/// Virtual default destructor because of inheritance.
		virtual ~Texture() override = default;

	protected:
		/// Default constructor. Protected, so this class can't be constructed directly.
		Texture();

	public:
		/// Load the texture contents from stream.
		virtual void Load(std::unique_ptr<std::istream>& stream);

		virtual void LoadRaw(void* data, size_t dataSize) = 0;

		/// Unloads the header file.
		virtual void Unload();

		/// Binds this texture to a given shader stage and slot index.
		virtual void Bind(ShaderType shader, uint32 slot = 0) = 0;

		/// Gets the width of this texture in pixels.
		virtual uint16 GetWidth() const { return m_header.width; }

		/// Gets the height of this texture in pixels.
		virtual uint16 GetHeight() const { return m_header.height; }

		/// Gets the memory usage of this texture in bytes on the gpu.
		virtual uint32 GetMemorySize() const { return 0; }

		/// Gets the underlying texture object.
		virtual void* GetTextureObject() const = 0;

		virtual void* GetRawTexture() const = 0;

		virtual void UpdateFromMemory(void* data, size_t dataSize) = 0;

		virtual void CopyPixelDataTo(uint8* destination) = 0;

		virtual uint32 GetPixelDataSize() const = 0;

		virtual PixelFormat GetPixelFormat() const;

		virtual TextureAddressMode GetTextureAddressModeU() const { return m_addressModeU; }

		virtual TextureAddressMode GetTextureAddressModeV() const { return m_addressModeV; }

		virtual TextureAddressMode GetTextureAddressModeW() const { return m_addressModeW; }

		virtual void SetTextureAddressMode(const TextureAddressMode mode) { m_addressModeU = m_addressModeV = m_addressModeW = mode; }

		virtual void SetTextureAddressModeU(const TextureAddressMode mode) { m_addressModeU = mode; }

		virtual void SetTextureAddressModeV(const TextureAddressMode mode) { m_addressModeV = mode; }

		virtual void SetTextureAddressModeW(const TextureAddressMode mode) { m_addressModeW = mode; }

		virtual TextureFilter GetTextureFilter() const { return m_filter; }
		
		virtual void SetFilter(const TextureFilter filter) { m_filter = filter; }

		void SetDebugName(String debugName);

		[[nodiscard]] const String& GetDebugName() const { return m_debugName; }

	protected:
		/// Texture file header.
		tex::v1_0::Header m_header;
		TextureAddressMode m_addressModeU { TextureAddressMode::Wrap };
		TextureAddressMode m_addressModeV { TextureAddressMode::Wrap };
		TextureAddressMode m_addressModeW { TextureAddressMode::Wrap };
		TextureFilter m_filter { TextureFilter::Anisotropic };
		String m_debugName;
		uint32 m_mipCount = 1;
	};

	/// A texture pointer object.
	typedef std::shared_ptr<Texture> TexturePtr;
}
