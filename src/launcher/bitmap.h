// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "color.h"
#include "geometry.h"
#include "resource_data.h"

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	/// An owning image surface in premultiplied BGRA.
	class Bitmap final
	{
	public:
		Bitmap() = default;
		Bitmap(int32 width, int32 height);

		/// Decodes a PNG from memory into premultiplied BGRA.
		/// Returns false and logs on failure; never throws.
		static bool DecodePng(const uint8* data, size_t size, Bitmap& out);

		/// Convenience overload for an embedded resource.
		static bool DecodePng(const ResourceBlob& blob, Bitmap& out);

		/// Filtered resample. Premultiplication aware, so it is safe across alpha edges.
		/// Returns false and logs on failure.
		static bool Resample(const Bitmap& src, int32 width, int32 height, Bitmap& out);

		bool IsValid() const { return m_width > 0 && m_height > 0; }
		int32 GetWidth() const { return m_width; }
		int32 GetHeight() const { return m_height; }
		Rect GetBounds() const { return Rect{ 0, 0, m_width, m_height }; }

		const Color* GetPixels() const { return m_pixels.data(); }
		Color* GetPixels() { return m_pixels.data(); }

		const Color* GetRow(const int32 y) const { return m_pixels.data() + static_cast<size_t>(y) * m_width; }
		Color* GetRow(const int32 y) { return m_pixels.data() + static_cast<size_t>(y) * m_width; }

	private:
		std::vector<Color> m_pixels;
		int32 m_width = 0;
		int32 m_height = 0;
	};
}
