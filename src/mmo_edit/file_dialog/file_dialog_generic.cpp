// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Fallback implementation for platforms that do not have a native file dialog
// implementation yet. The Windows implementation lives in win/file_dialog_win.cpp
// and is the only one compiled on Windows, so this translation unit is empty there.

#if !defined(_WIN32)

#include "file_dialog/file_dialog.h"

#include "log/default_log_levels.h"

namespace mmo
{
	std::optional<String> FileDialog::ShowSave(const String& title,
		const std::vector<FileDialogFilter>& filters,
		const String& defaultFileName,
		const String& defaultExtension)
	{
		WLOG("Native save file dialog is not implemented on this platform");
		return std::nullopt;
	}

	std::optional<String> FileDialog::ShowOpen(const String& title,
		const std::vector<FileDialogFilter>& filters)
	{
		WLOG("Native open file dialog is not implemented on this platform");
		return std::nullopt;
	}
}

#endif
