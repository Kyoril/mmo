// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "rect.h"
#include "size.h"
#include "color.h"


namespace mmo
{
	class FontImageset;
	class GeometryBuffer;

	/// Represents a named area on an imageset. This class is used to calculate texture coordinates
	/// for rendering text, but it also has a Draw method to create geometry.
	class FontImage
	{
	public:
		FontImage();
		explicit FontImage(const FontImageset& owner, const Rect& area, const Point& renderOffset, float horzScaling, float vertScaling);

		FontImage(FontImage&&) = default;
		FontImage& operator=(FontImage&&) = default;
		FontImage(const FontImage&) = default;
		FontImage& operator=(const FontImage&) = default;

	public:
		/// Gets the size of this image in pixels.
		inline const Size& GetSize() const { return m_scaledSize; }
		/// Gets the width of this image in pixels.
		inline float GetWidth() const { return m_scaledSize.width; }
		/// Gets the height of this image in pixels.
		inline float GetHeight() const { return m_scaledSize.height; }
		/// Gets the start offset of this image in pixels.
		inline const Point& GetOffset() const { return m_scaledOffset; }
		/// Gets the x coordinate of the start offset of this image in pixels.
		inline float GetOffsetX() const { return m_scaledOffset.x; }
		/// Gets the y coordinate of the start offset of this image in pixels.
		inline float GetOffsetY() const { return m_scaledOffset.y; }

	public:
		/// A rectangle which describes the source texture area used by this image.
		const Rect& GetSourceTextureArea() const;
		/// Queues the image to be drawn.
		/// @param position The position of the image.
		/// @param size The size with which the image will be drawn.
		void Draw(const Point& position, const Size& size, GeometryBuffer& buffer, argb_t color = 0xffffffff) const;

	private:
		const FontImageset* m_owner;
		Rect m_area;
		Point m_offset;
		Size m_scaledSize;
		Point m_scaledOffset;
	};
}
