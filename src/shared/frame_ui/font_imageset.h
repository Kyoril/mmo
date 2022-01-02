// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "rect.h"
#include "size.h"
#include "font_image.h"
#include "color.h"

#include "base/typedefs.h"
#include "graphics/texture.h"

#include <list>


namespace mmo
{
	class GeometryBuffer;

	/// An imageset is a combination of a texture (most likely runtime-generated) and
	/// named areas (images).
	class FontImageset
	{
	public:
		/// Sets the texture of this imageset.
		void SetTexture(TexturePtr texture);
		/// Defines a new named area on this set.
		FontImage& DefineImage(const Point& position, const Size& size, const Point& renderOffset);
		/// Defines a new named area on this set.
		FontImage& DefineImage(const Rect& imageRect, const Point& renderOffset);
		/// Draws an image from the imageset.
		void Draw(const Rect& srcRect, const Rect& dstRect, GeometryBuffer& buffer, argb_t color = 0xffffffff) const;

	private:

		/// Gets the texture id of this font.
		TexturePtr m_texture;
		/// Contains a list of all images.
		std::list<FontImage> m_images;
	};
}
