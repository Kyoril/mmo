// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <filesystem>
#include <vector>

namespace mmo
{
	/// @brief A 16-bit single-channel image (used for heightmaps).
	struct GrayImage16
	{
		/// @brief Width in pixels.
		uint32 width = 0;

		/// @brief Height in pixels.
		uint32 height = 0;

		/// @brief Row-major pixel values.
		std::vector<uint16> pixels;
	};

	/// @brief An 8-bit RGB image (used for previews).
	struct RgbImage8
	{
		/// @brief Width in pixels.
		uint32 width = 0;

		/// @brief Height in pixels.
		uint32 height = 0;

		/// @brief Row-major interleaved RGB triplets.
		std::vector<uint8> pixels;
	};

	/// @brief Loads a grayscale PNG as 16-bit image. 8-bit inputs are scaled to 16 bit,
	///	       multi-channel inputs are converted to grayscale.
	///	@param path Path of the PNG file to load.
	///	@param out Receives the image data.
	///	@return true on success.
	bool LoadGray16Png(const std::filesystem::path &path, GrayImage16 &out);

	/// @brief Saves a 16-bit grayscale PNG.
	///	@param path Path of the PNG file to write.
	///	@param image The image to save.
	///	@return true on success.
	bool SaveGray16Png(const std::filesystem::path &path, const GrayImage16 &image);

	/// @brief Saves an 8-bit RGB PNG.
	///	@param path Path of the PNG file to write.
	///	@param image The image to save.
	///	@return true on success.
	bool SaveRgb8Png(const std::filesystem::path &path, const RgbImage8 &image);
}
