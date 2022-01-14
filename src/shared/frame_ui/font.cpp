// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "font.h"

#include "base/macros.h"
#include "assets/asset_registry.h"
#include "graphics/graphics_device.h"

#include <atomic>
#include <fstream>

#include FT_STROKER_H


namespace mmo
{
	/// Amount of pixels to put between two glyphs
	static constexpr uint32 INTER_GLYPH_PAD_SPACE = 4;
	/// A multiplication coefficient to convert FT_Pos values into normal floats
	static constexpr float FT_POS_COEF = (1.0f / 64.0f);
	/// Number of glyphs per page. Must be a power of two.
	static constexpr uint32 GLYPHS_PER_PAGE = 256;
	/// Amount of bits in a uint32.
	static constexpr uint32 BITS_PER_UINT = sizeof(uint32) * 8;


	/// FreeType library handle.
	static FT_Library s_freeTypeLib;
	/// Usage counter of free type library handle.
	static std::atomic<int32> s_freeTypeUsageCount = 0;


	Font::Font()
		: m_pointSize(0.0f)
		, m_ascender(0)
		, m_descender(0)
		, m_height(0)
		, m_maxCodepoint(0)
		, m_outlineWidth(0.0f)
	{
		// Initializes the free type library if not already done and also increase
		// the reference counter.
		if (!s_freeTypeUsageCount++)
			FT_Init_FreeType(&s_freeTypeLib);
	}

	Font::~Font()
	{
		// Unload font face now before we eventually dispose the freetype library
		m_fontFace.reset();

		// Release freetype library handle if all using fonts are deleted
		if (!--s_freeTypeUsageCount)
			FT_Done_FreeType(s_freeTypeLib);
	}

	bool Font::Initialize(const std::string & filename, float pointSize, float outline)
	{
		// Try to load the font file
		auto filePtr = AssetRegistry::OpenFile(filename);
		ASSERT(filePtr);

		// Initialize data
		filePtr->seekg(0, std::ios::end);
		auto fileSize = filePtr->tellg();
		filePtr->seekg(0, std::ios::beg);

		// We need to keep the memory as long as we want to be able to rasterize
		// font glyphs, so we will copy the memory from the source area
		m_fileData.resize(fileSize);
		filePtr->read(reinterpret_cast<char*>(&m_fileData[0]), fileSize);

		// Apply point size
		m_pointSize = pointSize;
		m_outlineWidth = outline;

		// Negative outline size is not supported!
		if (m_outlineWidth < 0.0f) m_outlineWidth = 0.0f;

		// Now initialize the font
		return InitializeInternal();
	}

	bool Font::InitializeInternal()
	{
		// Save error code
		FT_Error error;

		// Initialize memory face
		FT_Face tmpFace = nullptr;
		if ((error = FT_New_Memory_Face(s_freeTypeLib, m_fileData.data(), static_cast<FT_Long>(m_fileData.size()), 0, &tmpFace)) != 0)
			return false;

		// Initialize smart pointer to automatically free the font if needed
		m_fontFace.reset(tmpFace);

		// Check that the default unicode character map is available
		if (!m_fontFace->charmap)
			return false;

		// Initialize the character size
		const std::int32_t dpi = 96;
		const float pointSize64 = m_pointSize * 64.0f;
		if (FT_Set_Char_Size(m_fontFace.get(), FT_F26Dot6(pointSize64), FT_F26Dot6(pointSize64), dpi, dpi))
		{
			// For bitmap fonts we can render only at specific point sizes.
			// Try to find nearest point size and use it, if that is possible
			const float pointSize72 = (m_pointSize * 72.0f) / dpi;
			float bestDelta = 99999.0f;
			float bestSize = 0.0f;
			for (int i = 0; i < m_fontFace->num_fixed_sizes; ++i)
			{
				float size = m_fontFace->available_sizes[i].size * FT_POS_COEF;
				float delta = ::fabs(size - pointSize72);
				if (delta < bestDelta)
				{
					bestDelta = delta;
					bestSize = size;
				}
			}

			// Check if we found a valid size and try to apply it
			if ((bestSize <= 0.0f) ||
				FT_Set_Char_Size(m_fontFace.get(), 0, FT_F26Dot6(bestSize * 64.0f), 0, 0))
				return false;
		}

		// We have the font size set up, get some informations
		if (m_fontFace->face_flags & FT_FACE_FLAG_SCALABLE)
		{
			const float yScale = m_fontFace->size->metrics.y_scale * FT_POS_COEF * (1.0f / 65536.0f);
			m_ascender = m_fontFace->ascender * yScale;
			m_descender = m_fontFace->descender * yScale;
			m_height = m_fontFace->height * yScale;
		}
		else
		{
			m_ascender = m_fontFace->size->metrics.ascender * FT_POS_COEF;
			m_descender = m_fontFace->size->metrics.descender * FT_POS_COEF;
			m_height = m_fontFace->size->metrics.height * FT_POS_COEF;
		}

		// Create an empty FontGlyph structure for every glyph of the font
		FT_UInt glyphIndex;
		FT_ULong codepoint = FT_Get_First_Char(m_fontFace.get(), &glyphIndex);
		FT_ULong maxCodepoint = codepoint;
		while (glyphIndex)
		{
			if (maxCodepoint < codepoint)
				maxCodepoint = codepoint;

			// Load-up required glyph metrics
			if (FT_Load_Char(m_fontFace.get(), codepoint, FT_LOAD_DEFAULT | FT_LOAD_FORCE_AUTOHINT))
				continue; // Glyph error

			// Create a new empty FontGlyph with given character code
			const float advance = m_fontFace->glyph->metrics.horiAdvance * FT_POS_COEF;
			m_glyphMap[codepoint] = FontGlyph(advance);

			// Proceed to next glyph
			codepoint = FT_Get_Next_Char(m_fontFace.get(), codepoint, &glyphIndex);
		}

		// Update the amount of code points
		SetMaxCodepoint(maxCodepoint);
		return true;
	}

	uint32 Font::GetTextureSize(GlyphMap::const_iterator start, GlyphMap::const_iterator end) const
	{
		/// A texture may only grow up to a 4k texture
		const uint32 maxTexsize = 4096;

		// Start with 32x32 pixel texture
		uint32 texSize = 32;
		uint32 glyphCount = 0;

		while (texSize < maxTexsize)
		{
			uint32 x = INTER_GLYPH_PAD_SPACE, y = INTER_GLYPH_PAD_SPACE;
			uint32 yb = INTER_GLYPH_PAD_SPACE;

			bool isTooSmall = false;
			for (GlyphMap::const_iterator c = start; c != end; ++c)
			{
				// Skip glpyhs that are already rendered
				if (c->second.GetImage())
					continue;	// Already rendered

				// Load glyph metrics
				if (FT_Load_Char(m_fontFace.get(), c->first, FT_LOAD_DEFAULT | FT_LOAD_FORCE_AUTOHINT))
					continue;	// Could not load glyph metrics, skip it!

				// Calculate the glyph size in pixels
				const uint32 glyphW = static_cast<uint32>(::ceil(m_fontFace->glyph->metrics.width * FT_POS_COEF)) + INTER_GLYPH_PAD_SPACE;
				const uint32 glyphH = static_cast<uint32>(::ceil(m_fontFace->glyph->metrics.height * FT_POS_COEF)) + INTER_GLYPH_PAD_SPACE;

				// Adjust the offset
				x += glyphW;
				if (x > texSize)
				{
					x = INTER_GLYPH_PAD_SPACE;
					y = yb;
				}

				uint32 yy = y + glyphH;
				if (yy > texSize)
					isTooSmall = true;

				if (yy > yb)
					yb = yy;

				++glyphCount;
			}

			// The texture size is enough for holding our glyphs
			if (!isTooSmall)
				break;

			// Double the texture size because it's too small
			texSize *= 2;
		}

		return glyphCount ? texSize : 0;
	}

	void Font::SetMaxCodepoint(uint32 codepoint)
	{
		m_maxCodepoint = codepoint;

		// Resize the glyph page map
		const uint32 pageCount = (codepoint + GLYPHS_PER_PAGE) / GLYPHS_PER_PAGE;
		const uint32 size = (pageCount + BITS_PER_UINT - 1) / BITS_PER_UINT;
		m_glyphPageLoaded.resize(size, 0);
	}

	void Font::Rasterize(uint32 startCodepoint, uint32 endCodepoint)
	{
		GlyphMap::const_iterator start = m_glyphMap.lower_bound(startCodepoint);
		if (start == m_glyphMap.end())
			return;

		GlyphMap::const_iterator origStart = start;
		GlyphMap::const_iterator end = m_glyphMap.upper_bound(endCodepoint);
		while (true)
		{
			// Create a new imageset for glyphs, so calculate how much texture size we will need
			uint32 texSize = GetTextureSize(start, end);
			if (!texSize)
				break;	// If all glyphs were already rendered, do nothing

			// Create the imageset
			m_imageSets.emplace_back(FontImageset());
			FontImageset* imageSet = &m_imageSets.back();

			// Create a memory buffer where the glyphs will be rendered and initialize it to 0
			std::vector<uint32> mem;
			mem.resize(texSize * texSize, 0);

			// Go ahead, line by line, top-left to bottom-right
			uint32 x = INTER_GLYPH_PAD_SPACE, y = INTER_GLYPH_PAD_SPACE;
			uint32 yb = INTER_GLYPH_PAD_SPACE;

			// Set to true when we finish rendering all glyphs we were asked to
			bool finished = false;
			// Set to false when we reach m_glyphMap.end() and we start going backward
			bool forward = true;

			// To converve texture space we will render more glyphs than asked, but never
			// less than asked. First we render all glyphs from start to end.
			// After that, we render glyphs until we reach m_glyphMap.end(), and if there
			// is still free texture space we will go backward from start, until we hit 
			// m_glyphMap.begin()
			while (start != m_glyphMap.end())
			{
				// Check if we finished rendering all the required glyphs
				finished |= (start == end);

				// Check if glyph already rendered
				if (!start->second.GetImage())
				{
					// Render the glyph
					if (FT_Load_Char(m_fontFace.get(), start->first, FT_LOAD_NO_BITMAP | FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_NORMAL))
					{
						// Error while rendering the glyph - use an empty image for this glyph!
						Rect area;
						Point offset;
						FontImage& image = imageSet->DefineImage(area, offset);
						((FontGlyph&)start->second).SetImage(&image);
					}
					else
					{
						if (m_fontFace->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
						{
							// Error while rendering the glyph - use an empty image for this glyph!
							Rect area;
							Point offset;
							FontImage& image = imageSet->DefineImage(area, offset);
							((FontGlyph&)start->second).SetImage(&image);
						}
						else
						{
							// Render normal glyph spans
							Spans spans;
							RenderSpans(s_freeTypeLib, &m_fontFace->glyph->outline, &spans);

							// Next we need the spans for the outline.
							Spans outlineSpans;
							if (m_outlineWidth > 0.0f)
							{
								FT_Stroker stroker;
								FT_Stroker_New(s_freeTypeLib, &stroker);
								FT_Stroker_Set(stroker, (int)(m_outlineWidth * 64.0f), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);

								FT_Glyph glyph;
								if (FT_Get_Glyph(m_fontFace->glyph, &glyph) == 0)
								{
									FT_Glyph_StrokeBorder(&glyph, stroker, 0, 1);
									// Again, this needs to be an outline to work.
									if (glyph->format == FT_GLYPH_FORMAT_OUTLINE)
									{
										// Render the outline spans to the span list
										FT_Outline *o =
											&reinterpret_cast<FT_OutlineGlyph>(glyph)->outline;
										RenderSpans(s_freeTypeLib, o, &outlineSpans);
									}

									// Clean up afterwards.
									FT_Stroker_Done(stroker);
									FT_Done_Glyph(glyph);
								}
							}

							// Now we need to put it all together.
							if (spans.empty())
							{
								// Error while rendering the glyph - use an empty image for this glyph!
								Rect area;
								Point offset;
								FontImage& image = imageSet->DefineImage(area, offset);
								((FontGlyph&)start->second).SetImage(&image);
							}
							else
							{
								// Calculate glyph bounds
								int minX = spans.front().x;
								int maxX = minX;
								int minY = spans.front().y;
								int maxY = minY;
								for (const auto& span : spans)
								{
									if (span.x < minX) minX = span.x;
									if (span.y < minY) minY = span.y;
									if (span.y > maxY) maxY = span.y;
									if (span.x + span.width > maxX) maxX = span.x + span.width;
								}
								for (const auto& span : outlineSpans)
								{
									if (span.x < minX) minX = span.x;
									if (span.y < minY) minY = span.y;
									if (span.y > maxY) maxY = span.y;
									if (span.x + span.width > maxX) maxX = span.x + span.width;
								}

								// Shortcut for glyph size
								const int32 glyphW = (int32)(maxX - minX) + INTER_GLYPH_PAD_SPACE;
								const int32 glyphH = (int32)(maxY - minY) + INTER_GLYPH_PAD_SPACE;

								// Check if glyph right margin does not exceed texture size
								uint32 xNext = x + glyphW;
								if (xNext > texSize)
								{
									x = INTER_GLYPH_PAD_SPACE;
									xNext = x + glyphW;
									y = yb;
								}

								// Check if glyph bottom margin does not exceed texture size
								uint32 yBot = y + glyphH;
								if (yBot > texSize)
									break;

								if (m_outlineWidth > 0.0f)
								{
									// Loop over the outline spans and just draw them into the image.
									for (Spans::iterator s = outlineSpans.begin(); s != outlineSpans.end(); ++s)
									{
										const int bufferX = (int)x + (s->x - minX) + s->width;
										const int bufferY = (int)((int)y + (int)glyphH - (s->y - minY));
										if (bufferX < static_cast<std::int32_t>(texSize) && bufferY >= 0 && bufferX - s->width >= 0 && bufferY < static_cast<std::int32_t>(texSize))
										{
											uint32* buffer = mem.data() + bufferY * static_cast<std::int32_t>(texSize) + bufferX;
											for (int w = 0; w < s->width; ++w)
											{
												*buffer-- = Pixel32(0, 0, 0, s->coverage).integer;
											}
										}
									}

									// Then loop over the regular glyph spans and blend them into the image.
									for (Spans::iterator s = spans.begin(); s != spans.end(); ++s)
									{
										const int bufferX = (int)x + (s->x - minX) + s->width;
										const int bufferY = (int)((int)y + (int)glyphH - (s->y - minY));
										if (bufferX < static_cast<std::int32_t>(texSize) && bufferY >= 0 && bufferX - s->width >= 0 && bufferY < static_cast<std::int32_t>(texSize))
										{
											uint32* buffer = mem.data() + bufferY * static_cast<int32>(texSize) + bufferX;

											for (int w = 0; w < s->width; ++w)
											{
												Pixel32 &dst = (Pixel32&)*buffer--;
												Pixel32 src = Pixel32(255, 255, 255, s->coverage);
												dst.r = (int)(dst.r + ((src.r - dst.r) * src.a) / 255.0f);
												dst.g = (int)(dst.g + ((src.g - dst.g) * src.a) / 255.0f);
												dst.b = (int)(dst.b + ((src.b - dst.b) * src.a) / 255.0f);
												dst.a = std::min(255, dst.a + src.a);
											}
										}
									}
								}
								else
								{
									// No outline, just render the spans
									for (Spans::iterator s = spans.begin(); s != spans.end(); ++s)
									{
										const int bufferX = (int)x + (s->x - minX) + s->width;
										const int bufferY = (int)((int)y + (int)glyphH - (s->y - minY));
										if (bufferX < static_cast<std::int32_t>(texSize) && bufferY >= 0 && bufferX - s->width >= 0 && bufferY < static_cast<std::int32_t>(texSize))
										{
											uint32* buffer = mem.data() + bufferY * static_cast<std::int32_t>(texSize) + bufferX;
											for (int w = 0; w < s->width; ++w)
											{
												*buffer-- = Pixel32(255, 255, 255, s->coverage).integer;
											}
										}
									}
								}

								Rect area(
									static_cast<float>(x),
									static_cast<float>(y + INTER_GLYPH_PAD_SPACE),
									static_cast<float>(x + glyphW),
									static_cast<float>(y + glyphH + INTER_GLYPH_PAD_SPACE));
								Point offset(
									m_fontFace->glyph->metrics.horiBearingX * FT_POS_COEF,
									-m_fontFace->glyph->metrics.horiBearingY * FT_POS_COEF + m_descender);

								FontImage& image = imageSet->DefineImage(area, offset);
								((FontGlyph&)start->second).SetImage(&image);

								// Advance to next position
								x = xNext;
								if (yBot > yb)
								{
									yb = yBot;
								}
							}
						}

					}
				}

				// Go to next glyph if we are going forward
				if (forward)
				{
					if (++start == m_glyphMap.end())
					{
						finished = true;
						forward = false;
						start = origStart;
					}
				}

				// Go to previous glyph if we are going backward
				if (!forward)
				{
					if ((start == m_glyphMap.begin()) || (--start == m_glyphMap.end()))
						break;
				}
			}

			// Manually create texture
			auto texture = GraphicsDevice::Get().CreateTexture(texSize, texSize);
			texture->LoadRaw(&mem[0], mem.size() * sizeof(uint32));
			imageSet->SetTexture(texture);

			// Check if finished
			if (finished)
				break;
		}
	}

	void Font::RasterCallback(const int y, const int count, const FT_Span * const spans, void * const user)
	{
		Spans *sptr = (Spans *)user;
		for (int i = 0; i < count; ++i)
			sptr->push_back(Span(spans[i].x, y, spans[i].len, spans[i].coverage));
	}

	void Font::RenderSpans(FT_Library & library, FT_Outline * const outline, Font::Spans * spans)
	{
		FT_Raster_Params params;
		memset(&params, 0, sizeof(params));
		params.flags = FT_RASTER_FLAG_AA | FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_CLIP;
		params.gray_spans = RasterCallback;
		params.user = spans;

		params.clip_box.xMin = -10000;
		params.clip_box.yMin = -10000;
		params.clip_box.xMax = 10000;
		params.clip_box.yMax = 10000;

		FT_Outline_Render(library, outline, &params);
	}

	float Font::GetTextWidth(const std::string & text, float scale)
	{
		float curWidth = 0.0f, advWidth = 0.0f, width = 0.0f;

		// Iterate through all characters of the string
		for (size_t c = 0; c < text.length(); ++c)
		{
			size_t iterations = 1;

			// Grab the glyph from string
			char g = text[c];
			if (g == '\t')
			{
				g = ' ';

				iterations = 4;
			}

			// Get the glyph data
			const FontGlyph* glyph = GetGlyphData(g);
			if (glyph)
			{
				// Adjust the width
				width = glyph->GetRenderedAdvance(scale);
				for (size_t i = 0; i < iterations; ++i)
				{
					if (advWidth + width > curWidth)
						curWidth = advWidth + width;

					advWidth += glyph->GetAdvance(scale);
				}
			}
		}

		return std::max(advWidth, curWidth);
	}

	const FontGlyph * Font::GetGlyphData(uint32 codepoint)
	{
		if (codepoint > m_maxCodepoint)
			return nullptr;

		// First see if the glyph data is already rasterized
		const uint32 page = codepoint / GLYPHS_PER_PAGE;
		const uint32 mask = 1 << (page & (BITS_PER_UINT - 1));
		if (!(m_glyphPageLoaded[page / BITS_PER_UINT] & mask))
		{
			// Not yet rasterized, so do it now and remember that we did it
			m_glyphPageLoaded[page / BITS_PER_UINT] |= mask;
			Rasterize(
				codepoint & ~(GLYPHS_PER_PAGE - 1),
				codepoint | (GLYPHS_PER_PAGE - 1));
		}

		// Fint the glyph data
		auto it = m_glyphMap.find(codepoint);
		return (it != m_glyphMap.end()) ? &it->second : nullptr;
	}

	void Font::DrawText(const std::string & text, const Point & position, GeometryBuffer& buffer, float scale, argb_t color)
	{
		const float baseY = position.y + GetBaseline(scale);
		Point glyphPos(position);

		for (size_t c = 0; c < text.length(); ++c)
		{
			size_t iterations = 1;

			char g = text[c];
			if (g == '\t')
			{
				g = ' ';
				iterations = 4;
			}

			const FontGlyph* glyph = nullptr;
			if ((glyph = GetGlyphData(g)))
			{
				const FontImage* const image = glyph->GetImage();
				glyphPos.y = baseY - (image->GetOffsetY() - image->GetOffsetY() * scale) + 4;

				image->Draw(glyphPos, glyph->GetImage()->GetSize() * scale, buffer, color);

				for (size_t i = 0; i < iterations; ++i)
				{
					glyphPos.x += glyph->GetAdvance(scale);
				}
			}
		}
	}
}
