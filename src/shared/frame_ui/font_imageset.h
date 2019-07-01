// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "rect.h"
#include "size.h"
#include "font_image.h"

#include "base/typedefs.h"
#include "graphics/texture.h"

#include <list>


namespace mmo
{
	class GeometryBuffer;

	class FontImageset
	{
	public:

		void SetTexture(TexturePtr texture);
		FontImage& DefineImage(const Point& position, const Size& size, const Point& renderOffset);
		FontImage& DefineImage(const Rect& imageRect, const Point& renderOffset);
		void Draw(const Rect& srcRect, const Rect& dstRect, GeometryBuffer& buffer) const;

	private:

		/// Gets the texture id of this font.
		TexturePtr m_texture;
		/// Contains a list of all images.
		std::list<FontImage> m_images;
	};
}
