// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "text_renderer.h"

#include "canvas.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <atomic>
#include <vector>

namespace mmo
{
	namespace
	{
		/// FreeType's library handle is shared by every face and must outlive all of
		/// them, so it is reference counted and torn down with the last face.
		FT_Library s_freeTypeLib = nullptr;
		std::atomic<int32> s_freeTypeUsageCount = 0;

		/// Decodes the next code point from a UTF-8 string, advancing `index`.
		/// Invalid sequences yield U+FFFD and consume one byte, so a malformed string
		/// can never loop forever.
		uint32 DecodeUtf8(const std::string& utf8, size_t& index)
		{
			const uint8 first = static_cast<uint8>(utf8[index]);

			int32 extraBytes = 0;
			uint32 codepoint = 0;

			if ((first & 0x80) == 0)
			{
				++index;
				return first;
			}
			else if ((first & 0xE0) == 0xC0)
			{
				extraBytes = 1;
				codepoint = first & 0x1F;
			}
			else if ((first & 0xF0) == 0xE0)
			{
				extraBytes = 2;
				codepoint = first & 0x0F;
			}
			else if ((first & 0xF8) == 0xF0)
			{
				extraBytes = 3;
				codepoint = first & 0x07;
			}
			else
			{
				++index;
				return 0xFFFD;
			}

			// The continuation bytes at index+1 .. index+extraBytes must all exist.
			if (index + extraBytes >= utf8.size())
			{
				++index;
				return 0xFFFD;
			}

			for (int32 i = 1; i <= extraBytes; ++i)
			{
				const uint8 continuation = static_cast<uint8>(utf8[index + i]);
				if ((continuation & 0xC0) != 0x80)
				{
					++index;
					return 0xFFFD;
				}

				codepoint = (codepoint << 6) | (continuation & 0x3F);
			}

			index += extraBytes + 1;
			return codepoint;
		}

		/// The eight offsets of a radius-N ring, used to dilate a glyph into an outline.
		template <typename F>
		void ForEachRingOffset(const int32 radius, F&& fn)
		{
			fn(-radius, -radius); fn(0, -radius); fn(radius, -radius);
			fn(-radius, 0);                       fn(radius, 0);
			fn(-radius, radius);  fn(0, radius);  fn(radius, radius);
		}
	}

	FontFace::FontFace()
	{
		if (s_freeTypeUsageCount++ == 0)
		{
			VERIFY(FT_Init_FreeType(&s_freeTypeLib) == 0);
		}
	}

	FontFace::~FontFace()
	{
		// The face must be destroyed before the library that created it.
		if (m_face)
		{
			FT_Done_Face(m_face);
			m_face = nullptr;
		}

		if (--s_freeTypeUsageCount == 0)
		{
			FT_Done_FreeType(s_freeTypeLib);
			s_freeTypeLib = nullptr;
		}
	}

	bool FontFace::Initialize(const ResourceBlob& blob, const int32 pixelHeight)
	{
		ASSERT(m_face == nullptr && "FontFace is already initialized");

		if (!blob.IsValid() || pixelHeight <= 0)
		{
			ELOG("Cannot initialize a font face from an invalid resource");
			return false;
		}

		// No copy of the font data is made: the blob addresses the module image and is
		// valid for the whole process lifetime, which is exactly what FT_New_Memory_Face
		// requires of its buffer.
		if (FT_New_Memory_Face(s_freeTypeLib, blob.data, static_cast<FT_Long>(blob.size), 0, &m_face) != 0)
		{
			ELOG("Failed to load embedded font face");
			m_face = nullptr;
			return false;
		}

		if (FT_Select_Charmap(m_face, FT_ENCODING_UNICODE) != 0)
		{
			WLOG("Embedded font has no unicode charmap; falling back to its default");
		}

		// FT_Set_Pixel_Sizes rather than FT_Set_Char_Size: DPI is already resolved into
		// a pixel height by the caller, and running that through a second DPI conversion
		// inside FreeType is how text ends up a pixel off.
		if (FT_Set_Pixel_Sizes(m_face, 0, static_cast<FT_UInt>(pixelHeight)) != 0)
		{
			ELOG("Failed to set font pixel size to " << pixelHeight);
			FT_Done_Face(m_face);
			m_face = nullptr;
			return false;
		}

		m_pixelHeight = pixelHeight;

		// Metrics are 26.6 fixed point.
		m_ascender = m_face->size->metrics.ascender >> 6;
		m_descender = m_face->size->metrics.descender >> 6;
		m_lineHeight = m_face->size->metrics.height >> 6;

		return true;
	}

	const Glyph* FontFace::GetGlyph(const uint32 codepoint)
	{
		ASSERT(m_face != nullptr);

		const auto it = m_glyphs.find(codepoint);
		if (it != m_glyphs.end())
		{
			return &it->second;
		}

		// FT_LOAD_FORCE_AUTOHINT: the display faces used here have poor built-in hints
		// at small pixel sizes, and the autohinter is more consistent across them.
		if (FT_Load_Char(m_face, codepoint, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_FORCE_AUTOHINT) != 0)
		{
			WLOG("Failed to rasterize glyph U+" << std::hex << codepoint << std::dec);
			return nullptr;
		}

		const FT_GlyphSlot slot = m_face->glyph;
		const FT_Bitmap& bitmap = slot->bitmap;

		Glyph glyph;
		glyph.width = static_cast<int32>(bitmap.width);
		glyph.height = static_cast<int32>(bitmap.rows);
		glyph.bearingX = slot->bitmap_left;
		glyph.bearingY = slot->bitmap_top;
		glyph.advance = static_cast<int32>(slot->advance.x >> 6);

		if (glyph.width > 0 && glyph.height > 0)
		{
			ASSERT(bitmap.pixel_mode == FT_PIXEL_MODE_GRAY);

			glyph.coverage.resize(static_cast<size_t>(glyph.width) * glyph.height);

			// pitch is signed: it is negative for bottom-up bitmaps, so rows must be
			// walked via the pitch rather than by assuming a positive stride.
			for (int32 y = 0; y < glyph.height; ++y)
			{
				const uint8* source = bitmap.buffer + static_cast<ptrdiff_t>(y) * bitmap.pitch;
				std::copy(source, source + glyph.width, glyph.coverage.begin() + static_cast<size_t>(y) * glyph.width);
			}
		}

		return &m_glyphs.emplace(codepoint, std::move(glyph)).first->second;
	}

	int32 FontFace::GetKerning(const uint32 left, const uint32 right) const
	{
		if (!m_face || !FT_HAS_KERNING(m_face))
		{
			return 0;
		}

		const FT_UInt leftIndex = FT_Get_Char_Index(m_face, left);
		const FT_UInt rightIndex = FT_Get_Char_Index(m_face, right);
		if (leftIndex == 0 || rightIndex == 0)
		{
			return 0;
		}

		FT_Vector kerning{};
		if (FT_Get_Kerning(m_face, leftIndex, rightIndex, FT_KERNING_DEFAULT, &kerning) != 0)
		{
			return 0;
		}

		return static_cast<int32>(kerning.x >> 6);
	}

	int32 MeasureText(FontFace& face, const std::string& utf8)
	{
		if (!face.IsValid() || utf8.empty())
		{
			return 0;
		}

		int32 width = 0;
		uint32 previous = 0;

		for (size_t i = 0; i < utf8.size(); )
		{
			const uint32 codepoint = DecodeUtf8(utf8, i);

			const Glyph* glyph = face.GetGlyph(codepoint);
			if (!glyph)
			{
				continue;
			}

			if (previous != 0)
			{
				width += face.GetKerning(previous, codepoint);
			}

			width += glyph->advance;
			previous = codepoint;
		}

		return width;
	}

	void DrawText(Canvas& canvas, FontFace& face, const std::string& utf8,
		const Rect& area, const TextAlign align, const TextStyle& style)
	{
		if (!face.IsValid() || utf8.empty())
		{
			return;
		}

		const int32 textWidth = MeasureText(face, utf8);

		int32 penX = area.left;
		switch (align)
		{
		case TextAlign::Center:
			penX = area.left + (area.GetWidth() - textWidth) / 2;
			break;
		case TextAlign::Right:
			penX = area.right - textWidth;
			break;
		case TextAlign::Left:
			break;
		}

		// Center the text box on the area using the face's own vertical metrics, so
		// that different strings in the same face sit on a common baseline.
		const int32 textHeight = face.GetAscender() - face.GetDescender();
		const int32 baseline = area.top + (area.GetHeight() - textHeight) / 2 + face.GetAscender();

		const bool hasOutline = style.outline.a > 0 && style.outlineWidth > 0;
		const bool hasShadow = style.shadow.a > 0 && (style.shadowOffset.x != 0 || style.shadowOffset.y != 0);

		uint32 previous = 0;

		for (size_t i = 0; i < utf8.size(); )
		{
			const uint32 codepoint = DecodeUtf8(utf8, i);

			const Glyph* glyph = face.GetGlyph(codepoint);
			if (!glyph)
			{
				continue;
			}

			if (previous != 0)
			{
				penX += face.GetKerning(previous, codepoint);
			}

			if (!glyph->coverage.empty())
			{
				const int32 x = penX + glyph->bearingX;
				const int32 y = baseline - glyph->bearingY;

				// Dilating the mask by blitting it around a ring is not as exact as
				// FT_Stroker, but at the sizes used here it is visually identical for a
				// fraction of the code. It thins out below roughly 14px; if a hairline
				// outline is ever needed at small sizes, switch to FT_Stroker.
				if (hasOutline)
				{
					ForEachRingOffset(style.outlineWidth, [&](const int32 dx, const int32 dy)
					{
						canvas.BlitCoverage(glyph->coverage.data(), glyph->width, glyph->height,
							glyph->width, x + dx, y + dy, style.outline);
					});
				}

				if (hasShadow)
				{
					canvas.BlitCoverage(glyph->coverage.data(), glyph->width, glyph->height,
						glyph->width, x + style.shadowOffset.x, y + style.shadowOffset.y, style.shadow);
				}

				canvas.BlitCoverage(glyph->coverage.data(), glyph->width, glyph->height,
					glyph->width, x, y, style.color);
			}

			penX += glyph->advance;
			previous = codepoint;
		}
	}
}
