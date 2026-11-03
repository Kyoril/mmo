// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>
#include <string>

namespace mmo
{
	/// Signature of a Localization.txt lookup: returns the translated string for a key,
	/// or nullptr if the current locale does not define that key.
	typedef std::function<const std::string* (const std::string&)> LocalizationLookup;

	/// @brief Builds the Localization.txt key for a class display name.
	///
	/// Class names live in the game data as plain english strings, so the key is derived
	/// from the name: "Scout" becomes "CLASS_SCOUT".
	///
	/// @param className The base (english) class name as stored in the game data.
	/// @return The uppercased key with a "CLASS_" prefix, or an empty string for an empty name.
	[[nodiscard]] std::string GetClassNameLocalizationKey(const std::string& className);

	/// @brief Resolves the localized display name of a class.
	///
	/// @param className The base (english) class name as stored in the game data. Has to outlive
	///        the returned reference, which aliases it when no translation exists.
	/// @param lookup The Localization.txt lookup to use. May be empty, in which case nothing is translated.
	/// @return The translation for the current locale, or @p className unchanged if there is none.
	[[nodiscard]] const std::string& LocalizeClassName(const std::string& className, const LocalizationLookup& lookup);
}
