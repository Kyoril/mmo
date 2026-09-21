// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/color_grading.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace mmo;

namespace
{
	/// A neutral strip LUT of edge size, 8-bit quantized like an imported PNG.
	struct StripImage
	{
		uint32 size = 0;
		std::vector<Vector3> texels;

		explicit StripImage(const uint32 edge)
			: size(edge)
			, texels(static_cast<size_t>(edge) * edge * edge)
		{
			const float scale = static_cast<float>(edge - 1);
			for (uint32 y = 0; y < edge; ++y)
			{
				for (uint32 x = 0; x < edge * edge; ++x)
				{
					const uint32 slice = x / edge;
					const uint32 column = x % edge;
					const auto quantize = [](const float value) { return std::round(value * 255.0f) / 255.0f; };
					texels[static_cast<size_t>(y) * edge * edge + x] = Vector3(
						quantize(static_cast<float>(column) / scale),
						quantize(static_cast<float>(y) / scale),
						quantize(static_cast<float>(slice) / scale));
				}
			}
		}

		[[nodiscard]] Vector3 Texel(int x, int y) const
		{
			const int width = static_cast<int>(size * size);
			x = std::clamp(x, 0, width - 1);
			y = std::clamp(y, 0, static_cast<int>(size) - 1);
			return texels[static_cast<size_t>(y) * width + x];
		}

		/// Bilinear sample at mip 0 with clamp addressing, like the LUT sampler.
		[[nodiscard]] Vector3 Sample(const float u, const float v) const
		{
			const float x = u * static_cast<float>(size * size) - 0.5f;
			const float y = v * static_cast<float>(size) - 0.5f;
			const int x0 = static_cast<int>(std::floor(x));
			const int y0 = static_cast<int>(std::floor(y));
			const float fx = x - static_cast<float>(x0);
			const float fy = y - static_cast<float>(y0);
			const Vector3 top = Texel(x0, y0) * (1.0f - fx) + Texel(x0 + 1, y0) * fx;
			const Vector3 bottom = Texel(x0, y0 + 1) * (1.0f - fx) + Texel(x0 + 1, y0 + 1) * fx;
			return top * (1.0f - fy) + bottom * fy;
		}

		/// The shader's two-read slice blend.
		[[nodiscard]] Vector3 Lookup(const float r, const float g, const float b) const
		{
			const color_grading::StripCoordinates c = color_grading::StripLutCoordinates(r, g, b, static_cast<float>(size));
			return Sample(c.u0, c.v) * (1.0f - c.sliceBlend) + Sample(c.u1, c.v) * c.sliceBlend;
		}
	};
}

TEST_CASE("Colour grading settings clamp to their range", "[color_grading]")
{
	ColorGradingSettings settings;
	CHECK(settings.saturation == Approx(1.0f));
	CHECK(settings.contrast == Approx(1.0f));
	CHECK(settings.colorFilter.x == Approx(1.0f));

	settings.SetSaturation(5.0f);
	CHECK(settings.saturation == Approx(2.0f));
	settings.SetSaturation(-1.0f);
	CHECK(settings.saturation == Approx(0.0f));

	settings.SetContrast(3.0f);
	CHECK(settings.contrast == Approx(2.0f));

	settings.SetColorFilter(Vector3(-1.0f, 0.5f, 9.0f));
	CHECK(settings.colorFilter.x == Approx(0.0f));
	CHECK(settings.colorFilter.y == Approx(0.5f));
	CHECK(settings.colorFilter.z == Approx(2.0f));
}

TEST_CASE("Strip LUTs are N*N wide and N high", "[color_grading]")
{
	CHECK(color_grading::IsStripLut(256, 16));
	CHECK(color_grading::IsStripLut(1024, 32));
	CHECK_FALSE(color_grading::IsStripLut(512, 32));
	CHECK_FALSE(color_grading::IsStripLut(1, 1));
	CHECK_FALSE(color_grading::IsStripLut(0, 0));
}

TEST_CASE("Strip coordinates address texel centres inside their slice", "[color_grading]")
{
	const color_grading::StripCoordinates black = color_grading::StripLutCoordinates(0.0f, 0.0f, 0.0f, 16.0f);
	CHECK(black.u0 == Approx(0.5f / 256.0f));
	CHECK(black.v == Approx(0.5f / 16.0f));
	CHECK(black.sliceBlend == Approx(0.0f));

	const color_grading::StripCoordinates white = color_grading::StripLutCoordinates(1.0f, 1.0f, 1.0f, 16.0f);
	CHECK(white.u0 == Approx(255.5f / 256.0f));
	CHECK(white.u1 == Approx(white.u0));
	CHECK(white.v == Approx(15.5f / 16.0f));

	// Blue halfway between slices 7 and 8 of a 16^3 LUT.
	const color_grading::StripCoordinates mid = color_grading::StripLutCoordinates(0.0f, 0.0f, 7.5f / 15.0f, 16.0f);
	CHECK(mid.u0 == Approx((7.0f * 16.0f + 0.5f) / 256.0f));
	CHECK(mid.u1 == Approx((8.0f * 16.0f + 0.5f) / 256.0f));
	CHECK(mid.sliceBlend == Approx(0.5f));

	// Out-of-range input clamps.
	const color_grading::StripCoordinates over = color_grading::StripLutCoordinates(2.0f, -1.0f, 3.0f, 32.0f);
	CHECK(over.u0 == Approx(1023.5f / 1024.0f));
	CHECK(over.v == Approx(0.5f / 32.0f));
}

TEST_CASE("A neutral strip LUT maps colours to themselves", "[color_grading]")
{
	for (const uint32 edge : { 16u, 32u })
	{
		const StripImage lut(edge);
		for (int ri = 0; ri <= 10; ++ri)
		{
			for (int gi = 0; gi <= 10; ++gi)
			{
				for (int bi = 0; bi <= 10; ++bi)
				{
					const float r = static_cast<float>(ri) / 10.0f;
					const float g = static_cast<float>(gi) / 10.0f;
					const float b = static_cast<float>(bi) / 10.0f;
					const Vector3 out = lut.Lookup(r, g, b);
					CHECK(std::abs(out.x - r) <= 1.0f / 255.0f + 1e-4f);
					CHECK(std::abs(out.y - g) <= 1.0f / 255.0f + 1e-4f);
					CHECK(std::abs(out.z - b) <= 1.0f / 255.0f + 1e-4f);
				}
			}
		}
	}
}
