// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/filesystem.h"
#include <string>
#include <optional>

namespace mmo
{
	namespace virtual_dir
	{
		struct IReader;
		struct IWriter;
	}

	namespace updating
	{
		/// Version metadata for patch manifests
		struct PatchVersionMetadata
		{
			std::string version;                  // Semantic version (e.g., "1.2.3")
			std::string buildDate;                // Build timestamp
			std::string gitCommit;                // Git commit SHA
			std::optional<std::string> gitBranch; // Git branch name
			std::optional<std::string> releaseNotes; // Release notes
		};
		
		/// Compiles a whole directory with all it's files and folders.
		/// @sourceDir A reader object for the source directory.
		/// @destinationDir A writer object for the destination directory.
		/// @param isZLibCompressed True to apply zlib compression on the files.
		/// @param versionMetadata Optional version metadata to include in manifest
		void compileDirectory(
			virtual_dir::IReader &sourceDir,
			virtual_dir::IWriter &destinationDir,
		    bool isZLibCompressed,
		    const PatchVersionMetadata* versionMetadata = nullptr
		);
	}
}
