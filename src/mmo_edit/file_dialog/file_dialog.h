// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <optional>
#include <vector>

namespace mmo
{
	/// @brief Describes a single file type filter for a native file dialog.
	struct FileDialogFilter
	{
		/// Human readable description shown in the dialog, e.g. "Item JSON".
		String name;
		/// File name pattern, e.g. "*.json". Multiple patterns can be separated by ';'.
		String pattern;

		FileDialogFilter() = default;
		FileDialogFilter(String filterName, String filterPattern)
			: name(std::move(filterName))
			, pattern(std::move(filterPattern))
		{
		}
	};

	/// @brief Thin cross platform wrapper around the operating system's native file dialogs.
	///
	/// Only the Windows implementation is provided right now. On platforms without an
	/// implementation the methods return std::nullopt and log a warning, so callers can
	/// always rely on "no value" meaning "no file was chosen".
	namespace FileDialog
	{
		/// @brief Shows a native "Save File" dialog.
		/// @param title Window title of the dialog.
		/// @param filters List of selectable file type filters.
		/// @param defaultFileName Optional file name suggested to the user.
		/// @param defaultExtension Extension appended automatically when the user omits one (without leading dot).
		/// @return The chosen absolute file path, or std::nullopt if the user cancelled.
		std::optional<String> ShowSave(const String& title,
			const std::vector<FileDialogFilter>& filters,
			const String& defaultFileName = String(),
			const String& defaultExtension = String());

		/// @brief Shows a native "Open File" dialog.
		/// @param title Window title of the dialog.
		/// @param filters List of selectable file type filters.
		/// @return The chosen absolute file path, or std::nullopt if the user cancelled.
		std::optional<String> ShowOpen(const String& title,
			const std::vector<FileDialogFilter>& filters);
	}
}
