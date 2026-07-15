// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "canvas.h"

#include "base/macros.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		/// Samples a bitmap with clamped point sampling.
		Color SampleNearest(const Bitmap& src, const int32 x, const int32 y)
		{
			const int32 cx = std::clamp(x, 0, src.GetWidth() - 1);
			const int32 cy = std::clamp(y, 0, src.GetHeight() - 1);
			return src.GetRow(cy)[cx];
		}

		/// Samples a bitmap bilinearly at a fixed point coordinate with 16 fractional bits.
		///
		/// Interpolating premultiplied components is correct across alpha edges; the same
		/// arithmetic on straight alpha would pull the color of fully transparent pixels
		/// into the result and halo the edge.
		Color SampleBilinear(const Bitmap& src, const int32 fx, const int32 fy)
		{
			constexpr int32 One = 1 << 16;

			const int32 x0 = fx >> 16;
			const int32 y0 = fy >> 16;
			const uint32 tx = static_cast<uint32>((fx & (One - 1)) >> 8);
			const uint32 ty = static_cast<uint32>((fy & (One - 1)) >> 8);

			const Color c00 = SampleNearest(src, x0, y0);
			const Color c10 = SampleNearest(src, x0 + 1, y0);
			const Color c01 = SampleNearest(src, x0, y0 + 1);
			const Color c11 = SampleNearest(src, x0 + 1, y0 + 1);

			const Color top = Lerp(c00, c10, static_cast<uint8>(tx));
			const Color bottom = Lerp(c01, c11, static_cast<uint8>(tx));
			return Lerp(top, bottom, static_cast<uint8>(ty));
		}
	}

	Canvas::Canvas(Color* pixels, const int32 width, const int32 height, const int32 stride)
		: m_pixels(pixels)
		, m_width(width)
		, m_height(height)
		, m_stride(stride)
	{
		ASSERT(pixels != nullptr);
		ASSERT(width > 0 && height > 0);
		ASSERT(stride >= width);

		m_clipStack.push_back(Rect{ 0, 0, width, height });
	}

	void Canvas::PushClip(const Rect& r)
	{
		m_clipStack.push_back(Intersect(GetClip(), r));
	}

	void Canvas::PopClip()
	{
		// The root clip is the surface itself and must always remain.
		ASSERT(m_clipStack.size() > 1);
		m_clipStack.pop_back();
	}

	void Canvas::Clear(const Color color)
	{
		const Rect clip = GetClip();
		if (clip.IsEmpty())
		{
			return;
		}

		for (int32 y = clip.top; y < clip.bottom; ++y)
		{
			Color* row = GetPixel(clip.left, y);
			std::fill(row, row + clip.GetWidth(), color);
		}
	}

	void Canvas::FillRect(const Rect& r, const Color color)
	{
		if (color.a == 0)
		{
			return;
		}

		const Rect area = Intersect(GetClip(), r);
		if (area.IsEmpty())
		{
			return;
		}

		// An opaque fill needs no blending at all.
		if (color.a == 255)
		{
			for (int32 y = area.top; y < area.bottom; ++y)
			{
				Color* row = GetPixel(area.left, y);
				std::fill(row, row + area.GetWidth(), color);
			}
			return;
		}

		for (int32 y = area.top; y < area.bottom; ++y)
		{
			Color* row = GetPixel(area.left, y);
			for (int32 x = 0; x < area.GetWidth(); ++x)
			{
				row[x] = BlendOver(color, row[x]);
			}
		}
	}

	void Canvas::FillGradient(const Rect& r, const Color from, const Color to, const bool horizontal)
	{
		const Rect area = Intersect(GetClip(), r);
		if (area.IsEmpty())
		{
			return;
		}

		// The ramp runs across the requested rect, not the clipped one, so clipping a
		// gradient does not rescale it.
		const int32 span = horizontal ? r.GetWidth() : r.GetHeight();
		if (span <= 0)
		{
			return;
		}

		for (int32 y = area.top; y < area.bottom; ++y)
		{
			Color* row = GetPixel(area.left, y);

			if (!horizontal)
			{
				const int32 t = ((y - r.top) * 255) / span;
				const Color color = Lerp(from, to, static_cast<uint8>(std::clamp(t, 0, 255)));
				if (color.a == 0)
				{
					continue;
				}

				for (int32 x = 0; x < area.GetWidth(); ++x)
				{
					row[x] = BlendOver(color, row[x]);
				}
				continue;
			}

			for (int32 x = 0; x < area.GetWidth(); ++x)
			{
				const int32 t = (((area.left + x) - r.left) * 255) / span;
				const Color color = Lerp(from, to, static_cast<uint8>(std::clamp(t, 0, 255)));
				if (color.a == 0)
				{
					continue;
				}

				row[x] = BlendOver(color, row[x]);
			}
		}
	}

	void Canvas::FillRoundedRect(const Rect& r, const int32 radius, const Color color)
	{
		if (color.a == 0)
		{
			return;
		}

		const Rect area = Intersect(GetClip(), r);
		if (area.IsEmpty())
		{
			return;
		}

		const int32 maxRadius = std::min(r.GetWidth(), r.GetHeight()) / 2;
		const int32 clampedRadius = std::clamp(radius, 0, maxRadius);
		if (clampedRadius == 0)
		{
			FillRect(r, color);
			return;
		}

		const float radiusF = static_cast<float>(clampedRadius);

		// Corner centres. A pixel is fully inside unless it falls in a corner box, so
		// only those need the distance test.
		const float leftCenter = static_cast<float>(r.left + clampedRadius) - 0.5f;
		const float rightCenter = static_cast<float>(r.right - clampedRadius) - 0.5f;
		const float topCenter = static_cast<float>(r.top + clampedRadius) - 0.5f;
		const float bottomCenter = static_cast<float>(r.bottom - clampedRadius) - 0.5f;

		for (int32 y = area.top; y < area.bottom; ++y)
		{
			Color* row = GetPixel(area.left, y);
			const float py = static_cast<float>(y);

			for (int32 x = 0; x < area.GetWidth(); ++x)
			{
				const float px = static_cast<float>(area.left + x);

				// Distance from the nearest corner centre, or 0 when not in a corner.
				const float dx = px < leftCenter ? leftCenter - px : (px > rightCenter ? px - rightCenter : 0.0f);
				const float dy = py < topCenter ? topCenter - py : (py > bottomCenter ? py - bottomCenter : 0.0f);

				float coverage = 1.0f;
				if (dx > 0.0f && dy > 0.0f)
				{
					// Approximate coverage by the signed distance to the arc, which is
					// smooth enough at these radii and costs one sqrt.
					const float distance = std::sqrt(dx * dx + dy * dy);
					coverage = std::clamp(radiusF + 0.5f - distance, 0.0f, 1.0f);
				}

				if (coverage <= 0.0f)
				{
					continue;
				}

				const Color sample = ScaleColor(color, static_cast<uint8>(coverage * 255.0f + 0.5f));
				row[x] = BlendOver(sample, row[x]);
			}
		}
	}

	void Canvas::DrawVignette(const Rect& r, const float strength)
	{
		if (strength <= 0.0f)
		{
			return;
		}

		const Rect area = Intersect(GetClip(), r);
		if (area.IsEmpty() || r.IsEmpty())
		{
			return;
		}

		const float halfWidth = static_cast<float>(r.GetWidth()) * 0.5f;
		const float halfHeight = static_cast<float>(r.GetHeight()) * 0.5f;
		const float centerX = static_cast<float>(r.left) + halfWidth;
		const float centerY = static_cast<float>(r.top) + halfHeight;

		for (int32 y = area.top; y < area.bottom; ++y)
		{
			Color* row = GetPixel(area.left, y);
			const float ny = (static_cast<float>(y) - centerY) / halfHeight;

			for (int32 x = 0; x < area.GetWidth(); ++x)
			{
				const float nx = (static_cast<float>(area.left + x) - centerX) / halfWidth;

				// Normalized radius, 0 at the centre and 1 at the edge midpoints.
				// Squared falloff keeps the centre clean and darkens only the corners.
				const float radius = std::sqrt(nx * nx + ny * ny) * 0.70710678f;
				const float falloff = std::clamp(radius, 0.0f, 1.0f);
				const uint8 alpha = static_cast<uint8>(falloff * falloff * strength * 255.0f + 0.5f);
				if (alpha == 0)
				{
					continue;
				}

				row[x] = BlendOver(Color{ 0, 0, 0, alpha }, row[x]);
			}
		}
	}

	void Canvas::Blit(const Bitmap& src, const Rect& srcRect, const Rect& dstRect, const BlitFilter filter)
	{
		BlitTinted(src, srcRect, dstRect, Color{ 255, 255, 255, 255 }, filter);
	}

	void Canvas::BlitTinted(const Bitmap& src, const Rect& srcRect, const Rect& dstRect,
		const Color tint, const BlitFilter filter)
	{
		if (!src.IsValid() || srcRect.IsEmpty() || dstRect.IsEmpty() || tint.a == 0)
		{
			return;
		}

		const Rect area = Intersect(GetClip(), dstRect);
		if (area.IsEmpty())
		{
			return;
		}

		const bool identityTint = tint.b == 255 && tint.g == 255 && tint.r == 255 && tint.a == 255;
		const bool scaled = srcRect.GetWidth() != dstRect.GetWidth() || srcRect.GetHeight() != dstRect.GetHeight();

		// Fixed point source stepping, 16 fractional bits.
		const int32 stepX = (srcRect.GetWidth() << 16) / dstRect.GetWidth();
		const int32 stepY = (srcRect.GetHeight() << 16) / dstRect.GetHeight();

		// Sample at pixel centres so that a 1:1 blit lands exactly on source texels and
		// a scaled one does not shift by half a pixel.
		const bool bilinear = scaled && filter == BlitFilter::Bilinear;
		const int32 halfTexelX = bilinear ? (stepX >> 1) - (1 << 15) : 0;
		const int32 halfTexelY = bilinear ? (stepY >> 1) - (1 << 15) : 0;

		for (int32 y = area.top; y < area.bottom; ++y)
		{
			const int32 fy = ((y - dstRect.top) * stepY) + (srcRect.top << 16) + halfTexelY;
			Color* row = GetPixel(area.left, y);

			for (int32 x = 0; x < area.GetWidth(); ++x)
			{
				const int32 fx = (((area.left + x) - dstRect.left) * stepX) + (srcRect.left << 16) + halfTexelX;

				Color sample = bilinear
					? SampleBilinear(src, fx, fy)
					: SampleNearest(src, fx >> 16, fy >> 16);

				if (sample.a == 0)
				{
					continue;
				}

				if (!identityTint)
				{
					sample = ModulateColor(sample, tint);
				}

				row[x] = sample.a == 255 ? sample : BlendOver(sample, row[x]);
			}
		}
	}

	void Canvas::BlitCoverage(const uint8* mask, const int32 maskWidth, const int32 maskHeight,
		const int32 maskStride, const int32 dstX, const int32 dstY, const Color color)
	{
		if (!mask || maskWidth <= 0 || maskHeight <= 0 || color.a == 0)
		{
			return;
		}

		const Rect area = Intersect(GetClip(), Rect{ dstX, dstY, dstX + maskWidth, dstY + maskHeight });
		if (area.IsEmpty())
		{
			return;
		}

		for (int32 y = area.top; y < area.bottom; ++y)
		{
			const uint8* maskRow = mask + static_cast<size_t>(y - dstY) * maskStride + (area.left - dstX);
			Color* row = GetPixel(area.left, y);

			for (int32 x = 0; x < area.GetWidth(); ++x)
			{
				const uint8 coverage = maskRow[x];
				if (coverage == 0)
				{
					continue;
				}

				// Scaling an already premultiplied color by coverage yields a still
				// premultiplied color at the reduced alpha, so this composites directly.
				row[x] = BlendOver(ScaleColor(color, coverage), row[x]);
			}
		}
	}

	void Canvas::BlitRegion(const Bitmap& src, const Rect& srcRect, const Rect& dstRect,
		const Color tint, const bool tile)
	{
		if (srcRect.IsEmpty() || dstRect.IsEmpty())
		{
			return;
		}

		if (!tile)
		{
			BlitTinted(src, srcRect, dstRect, tint);
			return;
		}

		// Repeat the source at its native size, clipping the final partial copy.
		PushClip(dstRect);

		for (int32 y = dstRect.top; y < dstRect.bottom; y += srcRect.GetHeight())
		{
			for (int32 x = dstRect.left; x < dstRect.right; x += srcRect.GetWidth())
			{
				const Rect target{ x, y, x + srcRect.GetWidth(), y + srcRect.GetHeight() };
				BlitTinted(src, srcRect, target, tint);
			}
		}

		PopClip();
	}

	void Canvas::DrawNineSlice(const Bitmap& src, const Insets& insets, const Rect& dstRect,
		const Color tint, const bool tileEdges, const NineSliceFill fill)
	{
		if (!src.IsValid() || dstRect.IsEmpty())
		{
			return;
		}

		const int32 sw = src.GetWidth();
		const int32 sh = src.GetHeight();

		// A destination narrower than its own corners must degrade rather than let the
		// regions overlap and double-blend. The progress fill hits this at value ~0.
		const int32 left = std::min(insets.left, dstRect.GetWidth() / 2);
		const int32 right = std::min(insets.right, dstRect.GetWidth() - left);
		const int32 top = std::min(insets.top, dstRect.GetHeight() / 2);
		const int32 bottom = std::min(insets.bottom, dstRect.GetHeight() - top);

		const int32 x0 = dstRect.left;
		const int32 x1 = dstRect.right;
		const int32 y0 = dstRect.top;
		const int32 y1 = dstRect.bottom;
		const int32 cx0 = x0 + left;
		const int32 cx1 = x1 - right;
		const int32 cy0 = y0 + top;
		const int32 cy1 = y1 - bottom;

		// Corners: never stretched.
		BlitTinted(src, Rect{ 0, 0, insets.left, insets.top }, Rect{ x0, y0, cx0, cy0 }, tint);
		BlitTinted(src, Rect{ sw - insets.right, 0, sw, insets.top }, Rect{ cx1, y0, x1, cy0 }, tint);
		BlitTinted(src, Rect{ 0, sh - insets.bottom, insets.left, sh }, Rect{ x0, cy1, cx0, y1 }, tint);
		BlitTinted(src, Rect{ sw - insets.right, sh - insets.bottom, sw, sh }, Rect{ cx1, cy1, x1, y1 }, tint);

		// Edges: stretched along one axis only.
		if (cx1 > cx0)
		{
			BlitRegion(src, Rect{ insets.left, 0, sw - insets.right, insets.top },
				Rect{ cx0, y0, cx1, cy0 }, tint, tileEdges);
			BlitRegion(src, Rect{ insets.left, sh - insets.bottom, sw - insets.right, sh },
				Rect{ cx0, cy1, cx1, y1 }, tint, tileEdges);
		}

		if (cy1 > cy0)
		{
			BlitRegion(src, Rect{ 0, insets.top, insets.left, sh - insets.bottom },
				Rect{ x0, cy0, cx0, cy1 }, tint, tileEdges);
			BlitRegion(src, Rect{ sw - insets.right, insets.top, sw, sh - insets.bottom },
				Rect{ cx1, cy0, x1, cy1 }, tint, tileEdges);
		}

		// Center.
		if (fill == NineSliceFill::Full && cx1 > cx0 && cy1 > cy0)
		{
			BlitRegion(src, Rect{ insets.left, insets.top, sw - insets.right, sh - insets.bottom },
				Rect{ cx0, cy0, cx1, cy1 }, tint, tileEdges);
		}
	}
}
