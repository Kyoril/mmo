// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <filesystem>

namespace mmo
{
	/// @brief Arguments of the preview subcommand.
	struct PreviewArgs
	{
		/// @brief The world name.
		String world;

		/// @brief First page column (inclusive).
		int32 pageX0 = 0;

		/// @brief First page row (inclusive).
		int32 pageZ0 = 0;

		/// @brief Last page column (inclusive).
		int32 pageX1 = 0;

		/// @brief Last page row (inclusive).
		int32 pageZ1 = 0;

		/// @brief Path of the preview PNG to write.
		std::filesystem::path outPath;

		/// @brief Contour line interval in world units (0 = no contours).
		float contourInterval = 0.0f;

		/// @brief Integer upscale factor for the output image (1 = one pixel per vertex).
		uint32 scale = 1;
	};

	/// @brief Renders a shaded relief preview image from terrain pages.
	/// @details Shading uses the normals stored in the page files (not recomputed ones), so
	///	         encoding or orientation bugs in imported terrain become visible here.
	///	@param args The preview arguments.
	///	@return Process exit code (0 on success).
	int32 RunPreview(const PreviewArgs &args);
}
