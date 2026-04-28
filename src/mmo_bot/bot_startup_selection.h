// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_startup_types.h"
#include "game/character_view.h"

#include <string_view>
#include <vector>

namespace mmo
{
	StartupProfileResolution ResolveStartupProfileSelection(
		std::string_view cliSelector,
		std::string_view configSelector,
		const std::vector<StartupProfileEntry>& availableProfiles);

	StartupCharacterResolution ResolveStartupCharacterSelection(
		std::string_view cliSelector,
		std::string_view configSelector,
		bool createIfMissing,
		const std::vector<CharacterView>& availableCharacters);
}
