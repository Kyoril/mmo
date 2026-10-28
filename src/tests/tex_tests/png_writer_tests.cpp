// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "tex/png_writer.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image/stb_image.h"

#include <vector>

using namespace mmo;

namespace
{
	std::vector<uint8> MakeGradient(const uint32 width, const uint32 height)
	{
		std::vector<uint8> pixels(static_cast<size_t>(width) * height * 4);
		for (uint32 y = 0; y < height; ++y)
		{
			for (uint32 x = 0; x < width; ++x)
			{
				uint8* p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
				p[0] = static_cast<uint8>(x * 17);
				p[1] = static_cast<uint8>(y * 29);
				p[2] = static_cast<uint8>((x + y) * 7);
				p[3] = static_cast<uint8>(255 - x);
			}
		}
		return pixels;
	}
}

TEST_CASE("EncodePng round-trips RGB pixels", "[png_writer]")
{
	constexpr uint32 width = 13;
	constexpr uint32 height = 7;
	const std::vector<uint8> pixels = MakeGradient(width, height);

	const std::vector<uint8> png = tex::EncodePng(width, height, pixels.data());
	REQUIRE(!png.empty());

	int w = 0, h = 0, channels = 0;
	stbi_uc* decoded = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &channels, 0);
	REQUIRE(decoded != nullptr);
	CHECK(w == static_cast<int>(width));
	CHECK(h == static_cast<int>(height));
	CHECK(channels == 3);

	bool same = true;
	for (uint32 i = 0; i < width * height; ++i)
	{
		for (uint32 c = 0; c < 3; ++c)
		{
			same = same && decoded[i * 3 + c] == pixels[i * 4 + c];
		}
	}
	CHECK(same);
	stbi_image_free(decoded);
}

TEST_CASE("EncodePng keeps alpha on request", "[png_writer]")
{
	constexpr uint32 width = 5;
	constexpr uint32 height = 4;
	const std::vector<uint8> pixels = MakeGradient(width, height);

	const std::vector<uint8> png = tex::EncodePng(width, height, pixels.data(), true);
	REQUIRE(!png.empty());

	int w = 0, h = 0, channels = 0;
	stbi_uc* decoded = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &channels, 0);
	REQUIRE(decoded != nullptr);
	CHECK(channels == 4);
	CHECK(std::equal(pixels.begin(), pixels.end(), decoded));
	stbi_image_free(decoded);
}

TEST_CASE("EncodePng rejects empty images", "[png_writer]")
{
	const uint8 pixel[4] = { 1, 2, 3, 4 };
	CHECK(tex::EncodePng(0, 1, pixel).empty());
	CHECK(tex::EncodePng(1, 0, pixel).empty());
	CHECK(tex::EncodePng(1, 1, nullptr).empty());
}
