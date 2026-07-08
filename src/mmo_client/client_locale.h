// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file client_locale.h
 *
 * @brief Access to the client's configured locale for localizing client-side game data.
 *
 * The locale is stored in the "locale" console variable and requires a restart to change,
 * so it is resolved once and cached. Use @ref GetClientLocale together with
 * @ref mmo::GetLocalizedString to display localized names/descriptions from the client DB
 * (e.g. spell names, tooltips) in the player's language.
 */

#pragma once

#include "base/localization.h"
#include "console/console_var.h"

namespace mmo
{
	/// Returns the client's configured locale (from the "locale" console variable), cached after first use.
	inline LocaleIndex GetClientLocale()
	{
		static const LocaleIndex locale = []()
		{
			const auto* cvar = ConsoleVarMgr::FindConsoleVar("locale");
			return cvar ? LocaleIndexFromString(cvar->GetStringValue()) : LocaleIndex::enUS;
		}();
		return locale;
	}
}
