// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <filesystem>
#include <vector>

#include "cook_report.h"

namespace mmo
{
	/// @brief Writes the warnings of a cook step as warnings.json: a sorted array of {"asset","message"}.
	/// @param path The file to write.
	/// @param warnings The warnings.
	/// @return false if the file could not be written.
	bool WriteWarningsFile(const std::filesystem::path& path, const std::vector<CookWarning>& warnings);

	/// @brief Reads a warnings.json written by WriteWarningsFile.
	/// @param path The file to read.
	/// @param out Receives the warnings.
	/// @return false if the file is missing or malformed.
	bool ReadWarningsFile(const std::filesystem::path& path, std::vector<CookWarning>& out);

	/// @brief Writes overlay.json into an overlay directory: {"schema":1,"delete":[...],"warnings":[...]}.
	///        The deployer lays the overlay's files over the data checkout, deletes the listed paths and
	///        reports the warnings.
	/// @param overlayDir The overlay directory.
	/// @param deletePaths Paths relative to the data directory to delete, written sorted.
	/// @param warnings The cook warnings, written sorted.
	/// @return false if the file could not be written.
	bool WriteOverlayManifest(const std::filesystem::path& overlayDir, const std::vector<String>& deletePaths, const std::vector<CookWarning>& warnings);
}
