// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "font_image.h"

#include "base/macros.h"


namespace mmo
{
	/// A font glyph contains data of a codepoint of a font. It also contains a link
	/// to an image, whose owning imageset is owned by the font.
	class FontGlyph
	{
	public:
		/// Initializes a new font glyph.
		explicit FontGlyph(float advance = 0.0f, FontImage* image = nullptr);

	public:

		/// Gets the horizontal advance value for the glyph.
		inline float GetAdvance(float scale) const { return m_advance * scale; }
		/// Gets the rendered advance value for this glyph.
		inline float GetRenderedAdvance(float scale = 1.0f) const { ASSERT(m_image); return (m_image->GetWidth() + m_image->GetOffsetX()) * scale; }
		/// Gets the image object which is rendered when this glyph is rendered.
		inline const FontImage* GetImage() const { return m_image; }
		/// Sets the image object which is rendered when this glyph is rendered.
		/// @param image The new image or nullptr to not use an image at all.
		inline void SetImage(const FontImage* image) { ASSERT(image); m_image = image; }

	private:

		/// The amount to advance the cursor after rendering this glyph.
		float m_advance;
		/// The image which will be rendered when this glyph is drawn.
		const FontImage* m_image;
	};
}
