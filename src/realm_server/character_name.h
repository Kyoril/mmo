// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>

namespace mmo
{
	/// Minimum number of characters (not bytes) a character name must have.
	constexpr std::size_t MinCharacterNameLength = 3;

	/// Maximum number of characters (not bytes) a character name may have.
	constexpr std::size_t MaxCharacterNameLength = 12;

	/// Checks whether a UTF-8 encoded string is an acceptable character name.
	/// @param name The name as received from the client, UTF-8 encoded.
	/// @returns true if the name has 3 to 12 characters, each of them an ASCII letter or one of
	///          the German letters ä, ö, ü, Ä, Ö, Ü and ß. Malformed UTF-8 is rejected.
	bool IsValidCharacterName(const std::string& name);
}
