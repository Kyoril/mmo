// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "font_glyph.h"
#include "font_imageset.h"
#include "point.h"
#include "color.h"

#include "base/typedefs.h"

#include <map>
#include <list>
#include <vector>
#include <memory>

#include "ft2build.h"
#include FT_FREETYPE_H


namespace mmo
{
	class GeometryBuffer;

	/// This class is used to load a font from a true type font file. It utilizes the freetype
	/// library to do this. It can also be used to measure text width and queue geometry for 
	/// drawing text to a GeometryBuffer object.
	class Font
	{
		/// A map that contains infos about loaded glyphs.
		typedef std::map<uint32, FontGlyph> GlyphMap;

	public:

		/// Default constructor. Eventually initializes the freetype library.
		Font();
		/// Destructor. Eventually terminates the freetype library.
		~Font();

	public:
		/// Loads the font file. It uses the AssetRegistry to do so.
		/// @param filename Name of the file to be loaded.
		/// @param pointSize The size of the font in points.
		/// @param outline The outline width in pixels.
		virtual bool Initialize(const std::string& filename, float pointSize, float outline = 0.0f);

	private:

		/// A span object is a horizontal pixel line that is generated by the freetype
		/// library when rasterizing a font glyph. It got coordinates, a width and
		/// a coverage value (which indicates the opacity). Think of it as a pixel,
		/// but more like a line of pixels who are neighbors and share the same opacity.
		struct Span
		{
			Span() = default;
			Span(int x_, int y_, int width_, int coverage_)
				: x{x_}
				, y{y_}
				, width{width_}
				, coverage{coverage_}
			{
			}

			int32 x, y, width, coverage;
		};

		/// A list of spans.
		typedef std::vector<Span> Spans;

		/// Helper type to work with uint32 color values to easily access separate color
		/// components.
		union Pixel32
		{
			Pixel32()
				: integer(0)
			{}
			Pixel32(uint8 bi, uint8 gi, uint8 ri, uint8 ai = 255)
			{
				b = bi;
				g = gi;
				r = ri;
				a = ai;
			}

			uint32 integer;
			struct
			{
				uint8 r, g, b, a;
			};
		};

	private:
		/// Performs internal initialization.
		bool InitializeInternal();
		/// Calculates the required texture size to display the given glyphs.
		/// @param start The start codepoint.
		/// @param end The end codepoint.
		/// @return The required texture size (textures are expected to be squared, so width == height).
		uint32 GetTextureSize(GlyphMap::const_iterator start, GlyphMap::const_iterator end) const;
		/// Sets the maximum supported codepoint and calculates the maximum page count.
		/// @param codepoint The maximum codepoint value.
		void SetMaxCodepoint(uint32 codepoint);
		/// Rasterizes the given codepoints, which means that textures will be generated if that
		/// didn't happen already.
		/// @param startCodepoint The start codepoint.
		/// @param endCodepoint The end codepoint.
		void Rasterize(uint32 startCodepoint, uint32 endCodepoint);

	private:
		// Static freetype callbacks

		/// A static raster callback that is required by the freetype library.
		static void RasterCallback(const int y, const int count, const FT_Span* const spans, void* const user);
		/// A static function render function that will rasterize the freetype-generated spans.
		static void RenderSpans(FT_Library& library, FT_Outline* const outline, Font::Spans *spans);

	public:
		/// Calculates the width in pixels of a given text.
		float GetTextWidth(const std::string& text, float scale = 1.0f);
		/// Tries to get glyph data of a certain codepoint. This eventually performs rasterization.
		/// @return nullptr if the data isn't available in this font.
		const FontGlyph* GetGlyphData(uint32 codepoint);
		/// Draws a given text by appending geometry to a given GeometryBuffer object.
		/// @param text The text to be drawn.
		/// @param position The position (in pixels) where to draw the text on screen.
		/// @param buffer The geometry buffer which will receive the generated geometry.
		/// @param scale A scaling factor. Keep in mind that upscaling will result in a loss of quality.
		/// @param color The argbv color value.
		void DrawText(const std::string& text, const Point& position, GeometryBuffer& buffer, float scale = 1.0f, argb_t color = 0xFFFFFFFF);

		int DrawText(const std::string& text, const Rect& area, GeometryBuffer* buffer, float scale = 1.0f, argb_t color = 0xFFFFFFFF);

		int GetLineCount(const std::string& text, const Rect& area, float scale = 1.0f);

	public:
		/// Gets the default spacing between two lines of text in pixels.
		/// @param scale A scaling factor. Keep in mind that upscaling will result in a loss of quality.
		inline float GetLineSpacing(float scale = 1.0f) const { return m_height * scale; }
		/// Gets the height of a whole line of text that is rendered using this font.
		/// @param scale A scaling factor. Keep in mind that upscaling will result in a loss of quality.
		inline float GetHeight(float scale = 1.0f) const { return (m_ascender - m_descender) * scale; }
		/// Gets the height of the baseline for text rendered using this font.
		/// @param scale A scaling factor. Keep in mind that upscaling will result in a loss of quality.
		inline float GetBaseline(float scale = 1.0f) const { return m_ascender * scale; }

	private:
		/// Helper object to call the destruction method on smart pointer delete.
		struct FT_FaceRec_Deleter
		{
			virtual void operator()(FT_FaceRec_* x)
			{
				FT_Done_Face(x);
			}
		};

		/// FreeType Face smart pointer with custom deleter
		typedef std::unique_ptr<FT_FaceRec_, FT_FaceRec_Deleter> FT_FacePtr;

	private:

		/// File data has to be kept for rasterization of the font.
		std::vector<uint8> m_fileData;
		/// The current font face.
		FT_FacePtr m_fontFace;
		/// The point size of this font.
		float m_pointSize;
		/// 
		float m_ascender;
		/// 
		float m_descender;
		/// 
		float m_height;
		/// The width of the outline of this font in pixels.
		float m_outlineWidth;
		/// A map of loaded glyph data.
		GlyphMap m_glyphMap;
		/// The maximum supported codepoint of this font.
		uint32 m_maxCodepoint;
		/// The loaded glyph pages.
		std::vector<uint32> m_glyphPageLoaded;
		/// The loaded imagesets. An imageset is a texture with image data. An image then is just
		/// a named area on the texture. In this case, each glyph should have one image that belongs
		/// to an imageset.
		std::list<FontImageset> m_imageSets;
	};

	/// A smart pointer typedef for fonts.
	typedef std::shared_ptr<Font> FontPtr;
}
