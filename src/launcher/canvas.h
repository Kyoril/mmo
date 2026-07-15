// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bitmap.h"
#include "color.h"
#include "geometry.h"

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	enum class BlitFilter
	{
		/// Point sampling. Correct only when source and destination sizes match.
		Nearest,
		/// Bilinear. Safe across alpha edges because the surface is premultiplied.
		Bilinear
	};

	enum class NineSliceFill
	{
		/// Draw all nine regions.
		Full,
		/// Draw the eight border regions and leave the middle untouched. Required for
		/// frame art whose center is opaque, which would otherwise paint over whatever
		/// the frame is supposed to be framing.
		FrameOnly
	};

	/// A drawing target over a caller-supplied premultiplied BGRA buffer.
	///
	/// The canvas does not own its pixels: on Win32 the buffer is the DIB section the
	/// window blits from, which is what keeps presenting a composited frame free of
	/// any copy or format conversion.
	class Canvas final
	{
	public:
		/// `stride` is measured in pixels, not bytes.
		Canvas(Color* pixels, int32 width, int32 height, int32 stride);

		int32 GetWidth() const { return m_width; }
		int32 GetHeight() const { return m_height; }
		Rect GetBounds() const { return Rect{ 0, 0, m_width, m_height }; }

		/// Intersects the current clip with `r`. Always pair with PopClip.
		void PushClip(const Rect& r);
		void PopClip();
		const Rect& GetClip() const { return m_clipStack.back(); }

		/// Overwrites without blending. For the opaque base fill.
		void Clear(Color color);

		/// Replaces the whole surface with `src`, which must match the canvas size.
		///
		/// Unlike Blit this overwrites rather than blends. The per-frame background
		/// restore needs that: the background carries transparent pixels now, and
		/// source-over would let each frame accumulate on top of the last instead of
		/// replacing it.
		void CopyFrom(const Bitmap& src);

		/// Source-over blend of a premultiplied color.
		void FillRect(const Rect& r, Color color);

		/// Linear gradient, interpolated in premultiplied space so that a
		/// transparent-to-opaque ramp does not fringe.
		void FillGradient(const Rect& r, Color from, Color to, bool horizontal);

		/// Anti-aliased rounded rectangle using analytic coverage.
		void FillRoundedRect(const Rect& r, int32 radius, Color color);

		/// Darkens towards the corners. `strength` is 0..1.
		void DrawVignette(const Rect& r, float strength);

		/// Source-over blit, resampling if the rects differ in size.
		void Blit(const Bitmap& src, const Rect& srcRect, const Rect& dstRect,
			BlitFilter filter = BlitFilter::Bilinear);

		/// As Blit, but modulates each source pixel by `tint`.
		void BlitTinted(const Bitmap& src, const Rect& srcRect, const Rect& dstRect,
			Color tint, BlitFilter filter = BlitFilter::Bilinear);

		/// Draws an 8 bit coverage mask (a rasterized glyph) as a solid colored shape.
		void BlitCoverage(const uint8* mask, int32 maskWidth, int32 maskHeight,
			int32 maskStride, int32 dstX, int32 dstY, Color color);

		/// Nine slice: corners drawn 1:1, edges stretched along one axis, center stretched.
		/// Insets are clamped when `dstRect` is smaller than the corners, so a degenerate
		/// destination degrades instead of corrupting.
		void DrawNineSlice(const Bitmap& src, const Insets& insets, const Rect& dstRect,
			Color tint = Color{ 255, 255, 255, 255 }, bool tileEdges = false,
			NineSliceFill fill = NineSliceFill::Full);

	private:
		Color* GetPixel(const int32 x, const int32 y) { return m_pixels + static_cast<size_t>(y) * m_stride + x; }

		/// Blits a single nine slice region, tiling along the stretch axis if requested.
		void BlitRegion(const Bitmap& src, const Rect& srcRect, const Rect& dstRect,
			Color tint, bool tile);

		Color* m_pixels;
		int32 m_width;
		int32 m_height;
		int32 m_stride;
		std::vector<Rect> m_clipStack;
	};
}
