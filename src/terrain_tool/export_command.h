// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <filesystem>

namespace mmo
{
	/// @brief Arguments of the export subcommand.
	struct ExportArgs
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

		/// @brief Path of the 16-bit grayscale heightmap PNG to write.
		std::filesystem::path outPath;

		/// @brief Path of the metadata JSON sidecar to write.
		std::filesystem::path metaOutPath;
	};

	/// @brief Exports terrain pages back into a heightmap image plus metadata sidecar.
	///	@param args The export arguments.
	///	@return Process exit code (0 on success).
	int32 RunExport(const ExportArgs &args);
}
