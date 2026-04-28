// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "base/constants.h"
#include "base/typedefs.h"

#include <optional>
#include <string>
#include <vector>

namespace mmo
{
	struct BotConfig
	{
		std::string loginHost { "mmo-dev.net" };
		uint16 loginPort { constants::DefaultLoginPlayerPort };
		std::string username;
		std::string password;

		std::string realmName;
		size_t realmIndex { 0 };

		std::string characterName { "Bot" };
		bool createCharacter { false };
		uint8 race { 0 };
		uint8 characterClass { 1 };
		uint8 gender { 0 };

		std::string greeting { "Hi" };
		bool randomMove { false };
		uint32 heartbeatMs { 5000 };

		std::string profileName { "simple_greeter" };
	};

	struct StartupProfileEntry
	{
		std::string key;
	};

	enum class StartupProfileResolutionKind
	{
		Resolved,
		NeedsPrompt,
		Invalid,
	};

	struct StartupProfileResolution
	{
		StartupProfileResolutionKind kind { StartupProfileResolutionKind::NeedsPrompt };
		std::string selector;
		std::string resolvedKey;
		std::optional<size_t> resolvedIndex;
		std::vector<size_t> candidateIndices;
	};

	enum class StartupCharacterResolutionKind
	{
		Resolved,
		NeedsPrompt,
		CreateCharacter,
		NotFound,
	};

	struct StartupCharacterResolution
	{
		StartupCharacterResolutionKind kind { StartupCharacterResolutionKind::NeedsPrompt };
		std::string selector;
		std::string resolvedName;
		std::optional<size_t> resolvedIndex;
		std::vector<size_t> candidateIndices;
	};
}
