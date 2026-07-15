// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "color.h"
#include "geometry.h"
#include "resource_data.h"

#include "base/typedefs.h"
#include "base/non_copyable.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "ft2build.h"
#include FT_FREETYPE_H

namespace mmo
{
	class Canvas;

	/// One rasterized glyph: an 8 bit coverage mask plus its placement metrics.
	struct Glyph
	{
		std::vector<uint8> coverage;
		int32 width = 0;
		int32 height = 0;
		int32 bearingX = 0;
		int32 bearingY = 0;
		int32 advance = 0;
	};

	/// A single TrueType face rasterized at a single pixel size.
	///
	/// Size is a property of the object rather than a parameter of GetGlyph on purpose.
	/// FreeType stores the size on the face, so a cache keyed by (codepoint, size) over
	/// one shared face would have to re-set the size on every miss, and any cached
	/// metric would silently belong to whichever size was set last. Making size part of
	/// the object makes that bug unrepresentable, and it means a DPI change is handled
	/// by building new faces rather than by invalidating a cache.
	///
	/// Not thread safe: owned and used by the UI thread only.
	class FontFace final : public NonCopyable
	{
	public:
		FontFace();
		~FontFace() override;

		/// `blob` must outlive this object. Embedded resources live for the process
		/// lifetime, so the face reads straight out of the binary with no copy.
		bool Initialize(const ResourceBlob& blob, int32 pixelHeight);

		bool IsValid() const { return m_face != nullptr; }

		/// Rasterizes on first use, then caches. Returns nullptr only if the face has
		/// no glyph for the codepoint and no .notdef.
		const Glyph* GetGlyph(uint32 codepoint);

		/// Horizontal kerning between two codepoints, in pixels. 0 when unsupported.
		int32 GetKerning(uint32 left, uint32 right) const;

		int32 GetAscender() const { return m_ascender; }
		int32 GetDescender() const { return m_descender; }
		int32 GetLineHeight() const { return m_lineHeight; }

	private:
		FT_Face m_face = nullptr;
		std::unordered_map<uint32, Glyph> m_glyphs;
		int32 m_pixelHeight = 0;
		int32 m_ascender = 0;
		int32 m_descender = 0;
		int32 m_lineHeight = 0;
	};

	enum class TextAlign
	{
		Left,
		Center,
		Right
	};

	/// How a text run is painted. Shadow and outline exist because the launcher draws
	/// text over arbitrary artwork, where a plain fill can lose all contrast.
	struct TextStyle
	{
		Color color = Color{ 255, 255, 255, 255 };

		/// Drawn behind the fill at `shadowOffset`. Disabled when the offset is zero.
		Color shadow = Color{ 0, 0, 0, 0 };
		Point shadowOffset{ 0, 0 };

		/// Drawn as a ring around the fill. Disabled when alpha or width is zero.
		Color outline = Color{ 0, 0, 0, 0 };
		int32 outlineWidth = 0;
	};

	/// Measures a single line UTF-8 run in pixels. Ignores shadow and outline.
	int32 MeasureText(FontFace& face, const std::string& utf8);

	/// Shortens `utf8` with a trailing ellipsis until it fits `maxWidth`.
	///
	/// Status text comes from the network stack and can be arbitrarily long, so it
	/// cannot be assumed to fit. Cutting on a code point boundary keeps the result
	/// valid UTF-8.
	std::string ElideText(FontFace& face, const std::string& utf8, int32 maxWidth);

	/// Draws a single line UTF-8 run, aligned horizontally within `area` and centered
	/// vertically on it. The caller is responsible for clipping.
	void DrawText(Canvas& canvas, FontFace& face, const std::string& utf8,
		const Rect& area, TextAlign align, const TextStyle& style);
}
