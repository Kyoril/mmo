// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <filesystem>

namespace mmo
{
	/// ~/Library/Logs/MMORPG, the macOS home of what Windows keeps in Documents/MMORPG/Logs.
	/// Empty if the user's library cannot be located.
	std::filesystem::path GetLauncherLogDirectory();

	/// ~/Library/Caches/AlestiaOnline/Launcher, the counterpart of the LocalAppData cache.
	/// Empty if the user's library cannot be located.
	std::filesystem::path GetLauncherCacheDirectory();

	/// Shows the launcher version in a modal alert.
	void ShowLauncherVersionInfo();
}
