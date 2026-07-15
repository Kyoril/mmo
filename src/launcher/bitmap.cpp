// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bitmap.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#include "stb_image/stb_image.h"
#include "stb_image/stb_image_resize2.h"

namespace mmo
{
	Bitmap::Bitmap(const int32 width, const int32 height)
		: m_pixels(static_cast<size_t>(width) * height)
		, m_width(width)
		, m_height(height)
	{
		ASSERT(width >= 0 && height >= 0);
	}

	bool Bitmap::DecodePng(const uint8* data, const size_t size, Bitmap& out)
	{
		if (data == nullptr || size == 0)
		{
			ELOG("Cannot decode an empty image");
			return false;
		}

		int width = 0;
		int height = 0;
		int sourceChannels = 0;

		// Always request 4 channels: stb then hands back straight (non premultiplied)
		// RGBA regardless of what the file actually stored.
		stbi_uc* decoded = stbi_load_from_memory(
			data, static_cast<int>(size), &width, &height, &sourceChannels, 4);
		if (!decoded)
		{
			ELOG("Failed to decode image: " << stbi_failure_reason());
			return false;
		}

		out = Bitmap(width, height);

		const stbi_uc* source = decoded;
		Color* target = out.GetPixels();

		// Swizzle to BGRA and premultiply in the same pass.
		for (int32 i = 0; i < width * height; ++i, source += 4, ++target)
		{
			const uint8 alpha = source[3];
			target->b = MulDiv255(source[2], alpha);
			target->g = MulDiv255(source[1], alpha);
			target->r = MulDiv255(source[0], alpha);
			target->a = alpha;

			ASSERT(IsPremultiplied(*target));
		}

		stbi_image_free(decoded);
		return true;
	}

	bool Bitmap::DecodePng(const ResourceBlob& blob, Bitmap& out)
	{
		if (!blob.IsValid())
		{
			ELOG("Cannot decode image from an invalid resource blob");
			return false;
		}

		return DecodePng(blob.data, blob.size, out);
	}

	bool Bitmap::Resample(const Bitmap& src, const int32 width, const int32 height, Bitmap& out)
	{
		if (!src.IsValid() || width <= 0 || height <= 0)
		{
			ELOG("Cannot resample an invalid image");
			return false;
		}

		Bitmap result(width, height);

		// STBIR_BGRA_PM tells stb the data is already premultiplied, so it filters in
		// premultiplied space and does not try to un-premultiply first.
		const void* resized = stbir_resize_uint8_srgb(
			reinterpret_cast<const unsigned char*>(src.GetPixels()),
			src.GetWidth(),
			src.GetHeight(),
			src.GetWidth() * static_cast<int>(sizeof(Color)),
			reinterpret_cast<unsigned char*>(result.GetPixels()),
			width,
			height,
			width * static_cast<int>(sizeof(Color)),
			STBIR_BGRA_PM);

		if (!resized)
		{
			ELOG("Failed to resample image to " << width << "x" << height);
			return false;
		}

		out = std::move(result);
		return true;
	}
}
