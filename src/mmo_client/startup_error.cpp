// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "startup_error.h"

#include "platform.h"

#include "log/default_log_levels.h"

#include <filesystem>

namespace mmo
{
	namespace
	{
		/// Resolves the config file path for display.
		/// @remarks The path is relative to the working directory, which is not necessarily the
		///          directory the executable sits in, so telling the player to look next to the
		///          game would be a guess. Resolve it and name the actual file.
		std::string GetDisplayConfigFilePath()
		{
			std::error_code error;
			const std::filesystem::path absolutePath = std::filesystem::absolute(ClientConfigFilePath, error);

			return error ? std::string(ClientConfigFilePath) : absolutePath.string();
		}
	}

	/// @copydoc ShowStartupError
	void ShowStartupError(const std::string& details)
	{
		ELOG(details);

		Platform::ShowErrorDialog("Game data not found",
			details + "\n\n"
			"The game cannot start without its data. If you did not move the game data on purpose, "
			"deleting this file resets the data path to its default and usually fixes the problem:\n\n"
			+ GetDisplayConfigFilePath());
	}
}
