// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "font_glyph.h"
#include "font_imageset.h"
#include "point.h"

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

	class Font
	{
		typedef std::map<uint32, FontGlyph> GlyphMap;

	public:

		Font();
		~Font();

	public:
		virtual bool Initialize(const std::string& filename, float pointSize, float outline = 0.0f);

	private:

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

		typedef std::vector<Span> Spans;

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
				uint8 r, g, b, a; //b, g, r, a;
			};
		};

	private:

		bool InitializeInternal();
		uint32 GetTextureSize(GlyphMap::const_iterator start, GlyphMap::const_iterator end) const;
		void SetMaxCodepoint(uint32 codepoint);
		void Rasterize(uint32 startCodepoint, uint32 endCodepoint);
		void DrawSpansToBuffer(uint32* buffer, uint32 textureSize, const Spans& spans) const;

	private:

		static void RasterCallback(const int y, const int count, const FT_Span* const spans, void* const user);
		static void RenderSpans(FT_Library& library, FT_Outline* const outline, Font::Spans *spans);

	public:

		float GetTextWidth(const std::string& text, float scale = 1.0f);
		const FontGlyph* GetGlyphData(uint32 codepoint);
		void DrawText(const std::string& text, const Point& position, GeometryBuffer& buffer, float scale = 1.0f);
		
	public:

		inline float GetLineSpacing(float scale = 1.0f) const { return m_height * scale; }
		inline float GetHeight(float scale = 1.0f) const { return (m_ascender - m_descender) * scale; }
		inline float GetBaseline(float scale = 1.0f) const { return m_ascender * scale; }

	private:

		struct FT_FaceRec_Deleter
		{
			virtual void operator()(FT_FaceRec_* x)
			{
				FT_Done_Face(x);
			}
		};

		typedef std::unique_ptr<FT_FaceRec_, FT_FaceRec_Deleter> FT_FacePtr;

	private:

		// File data has to be kept
		std::vector<uint8> m_fileData;

		FT_FacePtr m_fontFace;

		float m_pointSize;

		float m_ascender;

		float m_descender;

		float m_height;

		float m_outlineWidth;

		GlyphMap m_glyphMap;

		uint32 m_maxCodepoint;

		std::vector<uint32> m_glyphPageLoaded;

		std::list<FontImageset> m_imageSets;
	};
}
