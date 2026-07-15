// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// A 32 bit color in premultiplied BGRA order.
	///
	/// The byte order is deliberate: it is memory compatible with a top-down 32bpp
	/// BI_RGB DIB section on Windows and with CoreGraphics' kCGBitmapByteOrder32Little
	/// on macOS, so the composited surface can be handed to either platform with no
	/// conversion pass.
	///
	/// Premultiplication is likewise deliberate. It reduces the source-over operator
	/// to a single multiply-add per channel, and it is the only representation in
	/// which bilinear filtering across an alpha edge is correct -- interpolating
	/// straight alpha blends in whatever color the artist left in fully transparent
	/// pixels, which is where halos around UI art come from.
	struct Color
	{
		uint8 b = 0;
		uint8 g = 0;
		uint8 r = 0;
		uint8 a = 0;
	};

	static_assert(sizeof(Color) == 4, "Color must be tightly packed for direct surface access");

	/// Multiplies an 8 bit value by an 8 bit fraction with correct rounding and no
	/// division. Equivalent to (value * fraction + 127) / 255.
	constexpr uint8 MulDiv255(const uint32 value, const uint32 fraction)
	{
		const uint32 t = value * fraction + 128u;
		return static_cast<uint8>((t + (t >> 8)) >> 8);
	}

	/// Builds a premultiplied Color from straight (non premultiplied) components.
	constexpr Color MakeColor(const uint8 r, const uint8 g, const uint8 b, const uint8 a = 255)
	{
		return Color{ MulDiv255(b, a), MulDiv255(g, a), MulDiv255(r, a), a };
	}

	/// Builds a premultiplied Color from a 0xAARRGGBB literal. Intended for the theme tables.
	constexpr Color FromArgb(const uint32 argb)
	{
		return MakeColor(
			static_cast<uint8>((argb >> 16) & 0xFF),
			static_cast<uint8>((argb >> 8) & 0xFF),
			static_cast<uint8>(argb & 0xFF),
			static_cast<uint8>((argb >> 24) & 0xFF));
	}

	/// Scales every component of an already premultiplied color by scale/255.
	///
	/// All four components are scaled, which keeps the result premultiplied. This is
	/// what makes drawing a FreeType coverage mask a one liner: the coverage value is
	/// simply the scale.
	constexpr Color ScaleColor(const Color c, const uint8 scale)
	{
		return Color{ MulDiv255(c.b, scale), MulDiv255(c.g, scale), MulDiv255(c.r, scale), MulDiv255(c.a, scale) };
	}

	/// Composites a premultiplied source over a premultiplied destination.
	constexpr Color BlendOver(const Color src, const Color dst)
	{
		const uint32 inverseAlpha = 255u - src.a;
		return Color{
			static_cast<uint8>(src.b + MulDiv255(dst.b, inverseAlpha)),
			static_cast<uint8>(src.g + MulDiv255(dst.g, inverseAlpha)),
			static_cast<uint8>(src.r + MulDiv255(dst.r, inverseAlpha)),
			static_cast<uint8>(src.a + MulDiv255(dst.a, inverseAlpha))
		};
	}

	/// Multiplies two premultiplied colors component wise. The result stays premultiplied.
	constexpr Color ModulateColor(const Color a, const Color b)
	{
		return Color{ MulDiv255(a.b, b.b), MulDiv255(a.g, b.g), MulDiv255(a.r, b.r), MulDiv255(a.a, b.a) };
	}

	/// Linearly interpolates between two premultiplied colors. Interpolating in
	/// premultiplied space is what keeps a transparent-to-opaque gradient free of
	/// dark fringing.
	constexpr Color Lerp(const Color from, const Color to, const uint8 t)
	{
		const uint32 inverseT = 255u - t;
		return Color{
			static_cast<uint8>(MulDiv255(from.b, inverseT) + MulDiv255(to.b, t)),
			static_cast<uint8>(MulDiv255(from.g, inverseT) + MulDiv255(to.g, t)),
			static_cast<uint8>(MulDiv255(from.r, inverseT) + MulDiv255(to.r, t)),
			static_cast<uint8>(MulDiv255(from.a, inverseT) + MulDiv255(to.a, t))
		};
	}

	/// Whether a color satisfies the premultiplication invariant. Debug checks only.
	constexpr bool IsPremultiplied(const Color c)
	{
		return c.r <= c.a && c.g <= c.a && c.b <= c.a;
	}
}
