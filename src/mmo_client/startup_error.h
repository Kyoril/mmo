// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>

namespace mmo
{
	/// Path of the client config script, relative to the working directory.
	constexpr const char* ClientConfigFilePath = "Config/Config.cfg";

	/// Reports a startup failure that stops the client from booting.
	/// @remarks These are configuration or installation problems, not defects: a wrong data path,
	///          missing game data. Letting them run on into a crash produces a crash report that
	///          nobody can act on, so they are logged and shown to the player instead, together
	///          with the one thing the player can do about it.
	/// @param details A description of what exactly went wrong, shown above the advice.
	/// @param title Caption of the dialog.
	/// @param advice What the player can try, shown below the details. Empty means the default
	///        advice, which names the config file to delete — only correct when the data path
	///        itself is what went wrong, so anything else has to say what applies to it.
	void ShowStartupError(const std::string& details,
		const std::string& title = "Cannot start the game",
		const std::string& advice = std::string());
}
