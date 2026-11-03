// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "console/console_var.h"

#include <string>

namespace mmo
{
	/// @brief Name of the console variable holding whether the login screen remembers the account
	///        name between sessions. Only the account name is ever remembered, never the password.
	constexpr const char* RememberAccountNameCVar = "rememberAccountName";

	/// @brief Name of the console variable holding the account name of the last login, kept so
	///        that the login screen can prefill its account name field.
	constexpr const char* RememberedAccountNameCVar = "accountName";

	/// @brief Applies the "remember account name" setting to the remembered account name.
	/// @param rememberVar The rememberAccountName console variable.
	/// @param nameVar The accountName console variable, updated in place.
	/// @param accountName The account name to remember. Pass the stored value to only re-check it
	///        against the setting, which is what startup and toggling the option do.
	/// @return true if the stored name changed, meaning the config file has to be written.
	[[nodiscard]] inline bool ApplyRememberedAccountName(const ConsoleVar& rememberVar, ConsoleVar& nameVar, const std::string& accountName)
	{
		// Switching the option off has to erase what an earlier login stored, not just stop
		// updating it, otherwise the account name would stay behind in Config.cfg.
		const std::string newValue = rememberVar.GetBoolValue() ? accountName : std::string();
		if (newValue == nameVar.GetStringValue())
		{
			return false;
		}

		nameVar.Set(newValue);
		return true;
	}
}
