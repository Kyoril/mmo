// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "font_imageset.h"
#include "geometry_buffer.h"


namespace mmo
{
	void FontImageset::SetTexture(TexturePtr texture)
	{
		m_texture = texture;
	}

	FontImage & FontImageset::DefineImage(const Point & position, const Size & size, const Point & renderOffset)
	{
		return DefineImage(Rect(position, size), renderOffset);
	}

	FontImage & FontImageset::DefineImage(const Rect & imageRect, const Point & renderOffset)
	{
		m_images.emplace_back(FontImage(*this, imageRect, renderOffset, 1.0f, 1.0f));
		return m_images.back();
	}

	void FontImageset::Draw(const Rect & srcRect, const Rect & dstRect, GeometryBuffer & buffer, argb_t color) const
	{
		if (m_texture)
		{
			// Bind the texture object
			buffer.SetActiveTexture(m_texture);

			// Determine width and height
			const float w = (float)m_texture->GetWidth();
			const float h = (float)m_texture->GetHeight();

			const float srcU = srcRect.left / w;
			const float srcV = srcRect.top / h;
			const float dstU = srcRect.right / w;
			const float dstV = srcRect.bottom / h;

			// Setup geometry
			GeometryBuffer::Vertex vertices[6]{
				// First triangle
				{ { dstRect.left, dstRect.bottom, 0.0f }, color, { srcU, dstV } },
				{ { dstRect.left, dstRect.top, 0.0f }, color, { srcU, srcV } },
				{ { dstRect.right, dstRect.top, 0.0f }, color, { dstU, srcV } },
				// Second triangle
				{ { dstRect.right, dstRect.top, 0.0f }, color, { dstU, srcV } },
				{ { dstRect.right, dstRect.bottom, 0.0f }, color, { dstU, dstV } },
				{ { dstRect.left, dstRect.bottom, 0.0f }, color, { srcU, dstV } }
			};

			// Append vertices
			buffer.AppendGeometry(vertices, 6);
		}
	}
}
