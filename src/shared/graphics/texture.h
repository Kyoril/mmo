// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "tex_v1_0/header.h"

#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header_load.h"

#include "binary_io/stream_source.h"
#include "binary_io/reader.h"

#include "base/macros.h"

namespace mmo
{
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
		/// Gets the width of this texture in pixels.
		virtual uint16 GetWidth() const { return m_header.width; }
		/// Gets the height of this texture in pixels.
		virtual uint16 GetHeight() const { return m_header.height; }
		/// Gets the memory usage of this texture in bytes on the gpu.
		virtual uint32 GetMemorySize() const { return 0; }

	protected:
		/// Texture file header.
		tex::v1_0::Header m_header;
	};

	/// A texture pointer object.
	typedef std::shared_ptr<Texture> TexturePtr;
}
