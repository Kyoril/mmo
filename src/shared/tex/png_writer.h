// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <filesystem>
#include <vector>

namespace mmo
{
	namespace tex
	{
		/// @brief Encodes an 8 bit per channel RGBA image as a PNG file in memory.
		/// @param width Image width in pixels, greater than zero.
		/// @param height Image height in pixels, greater than zero.
		/// @param rgba Tightly packed pixel rows, top row first, four bytes per pixel.
		/// @param keepAlpha Whether to store the alpha channel. Without it, the image is stored as RGB.
		/// @return The encoded file, or an empty buffer if the input is invalid or compression failed.
		std::vector<uint8> EncodePng(uint32 width, uint32 height, const uint8* rgba, bool keepAlpha = false);

		/// @brief Encodes an 8 bit per channel RGBA image as a PNG file and writes it to disk.
		/// @param path Destination file. Its parent directory is created if it is missing.
		/// @param width Image width in pixels, greater than zero.
		/// @param height Image height in pixels, greater than zero.
		/// @param rgba Tightly packed pixel rows, top row first, four bytes per pixel.
		/// @param keepAlpha Whether to store the alpha channel. Without it, the image is stored as RGB.
		/// @return True if the whole file was written.
		bool WritePng(const std::filesystem::path& path, uint32 width, uint32 height, const uint8* rgba, bool keepAlpha = false);
	}
}
