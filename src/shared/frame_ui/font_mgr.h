// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "font.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/utilities.h"

#include <map>


namespace mmo
{
	/// This class manages fonts by name and properties to avoid having the same
	/// font loaded twice - similar to how the TextureManager of the graphics library
	/// tries to avoid loading the same texture twice.
	/// This is especially important since Fonts generate dynamic textures which contain
	/// the rasterized font glyphs that are used to render text.
	class FontManager final
		: public NonCopyable
	{
	public:
		/// Singleton getter method.
		static FontManager& Get();

	private:
		/// This is a static class that doesn't support instancing.
		FontManager() = default;

	public:
		/// Creates or retrieves a font.
		/// @param filename Filename of the font to load.
		/// @param size Font size in points.
		/// @param outline Outline width in pixels.
		FontPtr CreateOrRetrieve(const std::string& filename, float size, float outline = 0.0f);

	private:
		/// Determines whether the given font is already loaded using the given font size.
		FontPtr FindCachedFont(const std::string& filename, float size, float outline) const;

	private:
		typedef std::map<float, FontPtr> FontPtrByOutline;
		typedef std::map<float, FontPtrByOutline> FontPtrBySize;
		typedef std::map<std::string, FontPtrBySize, StrCaseIComp> FontCache;

		FontCache m_fontCache;
	};
}
