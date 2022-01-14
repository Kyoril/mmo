// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
	class Texture
	{
	public:
		/// Virtual default destructor because of inheritance.
		virtual ~Texture() = default;

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

		virtual TextureAddressMode GetTextureAddressModeU() const { return m_addressModeU; }
		virtual TextureAddressMode GetTextureAddressModeV() const { return m_addressModeV; }
		virtual TextureAddressMode GetTextureAddressModeW() const { return m_addressModeW; }
		virtual void SetTextureAddressMode(TextureAddressMode mode) { m_addressModeU = m_addressModeV = m_addressModeW = mode; }
		virtual void SetTextureAddressModeU(TextureAddressMode mode) { m_addressModeU = mode; }
		virtual void SetTextureAddressModeV(TextureAddressMode mode) { m_addressModeV = mode; }
		virtual void SetTextureAddressModeW(TextureAddressMode mode) { m_addressModeW = mode; }

		virtual TextureFilter GetTextureFilter() const { return m_filter; }
		
		virtual void SetFilter(TextureFilter filter) { m_filter = filter; }

	protected:
		/// Texture file header.
		tex::v1_0::Header m_header;
		TextureAddressMode m_addressModeU { TextureAddressMode::Wrap };
		TextureAddressMode m_addressModeV { TextureAddressMode::Wrap };
		TextureAddressMode m_addressModeW { TextureAddressMode::Wrap };
		TextureFilter m_filter { TextureFilter::Anisotropic };
	};

	/// A texture pointer object.
	typedef std::shared_ptr<Texture> TexturePtr;
}
