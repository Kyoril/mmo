// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mmo_bot/bot_console_prompt.h"
#include "mmo_bot/bot_startup_selection.h"

#include <sstream>

namespace mmo
{
	namespace
	{
		CharacterView MakeCharacter(uint64 guid, std::string name)
		{
			return CharacterView(
				guid,
				std::move(name),
				1,
				0,
				0,
				1,
				1,
				0,
				false,
				0);
		}
	}

	TEST_CASE("Startup profile selection prefers CLI selector over config selector", "[bot][startup][profile]")
	{
		const std::vector<StartupProfileEntry> availableProfiles = {
			{ "simple_greeter" },
			{ "combat" },
		};

		const StartupProfileResolution resolution = ResolveStartupProfileSelection(
			"combat",
			"simple_greeter",
			availableProfiles);

		REQUIRE(resolution.kind == StartupProfileResolutionKind::Resolved);
		REQUIRE(resolution.selector == "combat");
		REQUIRE(resolution.resolvedKey == "combat");
		REQUIRE(resolution.resolvedIndex == 1);
		REQUIRE(resolution.candidateIndices.empty());
	}

	TEST_CASE("Startup profile selection auto-resolves a single available profile when selectors are unspecified", "[bot][startup][profile]")
	{
		const std::vector<StartupProfileEntry> availableProfiles = {
			{ "simple_greeter" },
		};

		const StartupProfileResolution resolution = ResolveStartupProfileSelection(
			"   ",
			"",
			availableProfiles);

		REQUIRE(resolution.kind == StartupProfileResolutionKind::Resolved);
		REQUIRE(resolution.selector.empty());
		REQUIRE(resolution.resolvedKey == "simple_greeter");
		REQUIRE(resolution.resolvedIndex == 0);
	}

	TEST_CASE("Startup profile selection requests a prompt when multiple profiles are available without a selector", "[bot][startup][profile]")
	{
		const std::vector<StartupProfileEntry> availableProfiles = {
			{ "simple_greeter" },
			{ "combat" },
		};

		const StartupProfileResolution resolution = ResolveStartupProfileSelection(
			"",
			" ",
			availableProfiles);

		REQUIRE(resolution.kind == StartupProfileResolutionKind::NeedsPrompt);
		REQUIRE(resolution.selector.empty());
		REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0, 1 });
	}

	TEST_CASE("Startup profile selection rejects unknown selectors without silently falling back", "[bot][startup][profile]")
	{
		const std::vector<StartupProfileEntry> availableProfiles = {
			{ "simple_greeter" },
			{ "combat" },
		};

		const StartupProfileResolution resolution = ResolveStartupProfileSelection(
			"unknown",
			"simple_greeter",
			availableProfiles);

		REQUIRE(resolution.kind == StartupProfileResolutionKind::Invalid);
		REQUIRE(resolution.selector == "unknown");
		REQUIRE_FALSE(resolution.resolvedIndex.has_value());
		REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0, 1 });
	}

	TEST_CASE("Startup profile selection rejects duplicate case-insensitive profile keys", "[bot][startup][profile]")
	{
		const std::vector<StartupProfileEntry> availableProfiles = {
			{ "combat" },
			{ "Combat" },
		};

		const StartupProfileResolution resolution = ResolveStartupProfileSelection(
			"combat",
			"",
			availableProfiles);

		REQUIRE(resolution.kind == StartupProfileResolutionKind::Invalid);
		REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0, 1 });
	}

	TEST_CASE("Startup character selection auto-resolves a single available character", "[bot][startup][character]")
	{
		const std::vector<CharacterView> availableCharacters = {
			MakeCharacter(1, "BotOne"),
		};

		const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
			"",
			"",
			false,
			availableCharacters);

		REQUIRE(resolution.kind == StartupCharacterResolutionKind::Resolved);
		REQUIRE(resolution.selector.empty());
		REQUIRE(resolution.resolvedName == "BotOne");
		REQUIRE(resolution.resolvedIndex == 0);
	}

	TEST_CASE("Startup character selection prefers CLI selector and matches names case-insensitively", "[bot][startup][character]")
	{
		const std::vector<CharacterView> availableCharacters = {
			MakeCharacter(1, "MageOne"),
			MakeCharacter(2, "TankTwo"),
		};

		const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
			"tanktwo",
			"MageOne",
			false,
			availableCharacters);

		REQUIRE(resolution.kind == StartupCharacterResolutionKind::Resolved);
		REQUIRE(resolution.selector == "tanktwo");
		REQUIRE(resolution.resolvedName == "TankTwo");
		REQUIRE(resolution.resolvedIndex == 1);
	}

	TEST_CASE("Startup character selection honors config selector when CLI selector is omitted", "[bot][startup][character]")
	{
		const std::vector<CharacterView> availableCharacters = {
			MakeCharacter(1, "MageOne"),
			MakeCharacter(2, "TankTwo"),
		};

		const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
			" ",
			"TankTwo",
			false,
			availableCharacters);

		REQUIRE(resolution.kind == StartupCharacterResolutionKind::Resolved);
		REQUIRE(resolution.selector == "TankTwo");
		REQUIRE(resolution.resolvedName == "TankTwo");
		REQUIRE(resolution.resolvedIndex == 1);
	}

	TEST_CASE("Startup character selection requests a prompt when multiple characters are available without a selector", "[bot][startup][character]")
	{
		const std::vector<CharacterView> availableCharacters = {
			MakeCharacter(1, "MageOne"),
			MakeCharacter(2, "TankTwo"),
		};

		const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
			"",
			" ",
			false,
			availableCharacters);

		REQUIRE(resolution.kind == StartupCharacterResolutionKind::NeedsPrompt);
		REQUIRE(resolution.selector.empty());
		REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0, 1 });
	}

	TEST_CASE("Startup character selection requests a prompt for duplicate case-insensitive names", "[bot][startup][character]")
	{
		const std::vector<CharacterView> availableCharacters = {
			MakeCharacter(1, "MageOne"),
			MakeCharacter(2, "mageone"),
			MakeCharacter(3, "TankTwo"),
		};

		const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
			"MageOne",
			"",
			false,
			availableCharacters);

		REQUIRE(resolution.kind == StartupCharacterResolutionKind::NeedsPrompt);
		REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0, 1 });
	}

	TEST_CASE("Startup character selection requests a prompt when the character catalog contains empty names", "[bot][startup][character]")
	{
		const std::vector<CharacterView> availableCharacters = {
			MakeCharacter(1, "MageOne"),
			MakeCharacter(2, "   "),
		};

		const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
			"MageOne",
			"",
			false,
			availableCharacters);

		REQUIRE(resolution.kind == StartupCharacterResolutionKind::NeedsPrompt);
		REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0, 1 });
	}

	TEST_CASE("Startup character selection returns create_character only for a named missing target when enabled", "[bot][startup][character]")
	{
		const std::vector<CharacterView> availableCharacters = {
			MakeCharacter(1, "MageOne"),
		};

		const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
			"NewBot",
			"",
			true,
			availableCharacters);

		REQUIRE(resolution.kind == StartupCharacterResolutionKind::CreateCharacter);
		REQUIRE(resolution.selector == "NewBot");
		REQUIRE_FALSE(resolution.resolvedIndex.has_value());
		REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0 });
	}

	TEST_CASE("Startup character selection returns not_found for a named missing target when creation is disabled", "[bot][startup][character]")
	{
		const std::vector<CharacterView> availableCharacters = {
			MakeCharacter(1, "MageOne"),
		};

		const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
			"NewBot",
			"",
			false,
			availableCharacters);

		REQUIRE(resolution.kind == StartupCharacterResolutionKind::NotFound);
		REQUIRE(resolution.selector == "NewBot");
		REQUIRE_FALSE(resolution.resolvedIndex.has_value());
	}

	TEST_CASE("Startup console prompt refuses selection when stdin is non-interactive", "[bot][startup][prompt]")
	{
		std::istringstream input("1\n");
		std::ostringstream output;

		const BotConsolePromptResult result = PromptForSelection(
			"profile",
			std::vector<std::string>{ "simple_greeter", "combat" },
			input,
			output,
			false);

		REQUIRE(result.kind == BotConsolePromptResultKind::NonInteractive);
		REQUIRE_FALSE(result.selectedIndex.has_value());
	}

	TEST_CASE("Startup console prompt re-prompts on malformed selections until a valid index is chosen", "[bot][startup][prompt]")
	{
		std::istringstream input("abc\n0\n3\n2\n");
		std::ostringstream output;

		const BotConsolePromptResult result = PromptForSelection(
			"profile",
			std::vector<std::string>{ "simple_greeter", "combat" },
			input,
			output,
			true);

		REQUIRE(result.kind == BotConsolePromptResultKind::Selected);
		REQUIRE(result.selectedIndex == 1);
		REQUIRE(output.str().find("Select profile:") != std::string::npos);
		REQUIRE(output.str().find("Invalid selection") != std::string::npos);
	}

	TEST_CASE("Startup console prompt aborts when interactive input ends before a valid choice", "[bot][startup][prompt]")
	{
		std::istringstream input("");
		std::ostringstream output;

		const BotConsolePromptResult result = PromptForSelection(
			"profile",
			std::vector<std::string>{ "simple_greeter", "combat" },
			input,
			output,
			true);

		REQUIRE(result.kind == BotConsolePromptResultKind::Aborted);
		REQUIRE_FALSE(result.selectedIndex.has_value());
	}
}
