// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>
#include "base/filesystem.h"


namespace mmo
{
	/// Callback type for reporting extraction progress.
	/// @param totalProgress The total amount of progress in percent.
	/// @param currentFile The current absolute file path.
	typedef std::function<void (double totalProgress, const std::filesystem::path &currentFile)> ExtractionProgressCallback;


	/// Extracts all files from a given hpak archive to the given destination.
	/// @param archive The hpak file stream for reading from the archive.
	/// @param destination The destination path where the archive contents should be extracted to.
	/// @param callback A progress report callback.
	bool Extract(
	    std::istream &archive,
	    const std::filesystem::path &destination,
	    const ExtractionProgressCallback &callback
	);
}
