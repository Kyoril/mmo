// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mmo_bot/bot_startup_selection.h"

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

	TEST_CASE("Startup character selection requests a prompt for duplicate or malformed character names", "[bot][startup][character]")
	{
		SECTION("duplicate names differing only by case")
		{
			const std::vector<CharacterView> availableCharacters = {
				MakeCharacter(1, "BotOne"),
				MakeCharacter(2, "botone"),
			};

			const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
				"BotOne",
				"",
				false,
				availableCharacters);

			REQUIRE(resolution.kind == StartupCharacterResolutionKind::NeedsPrompt);
			REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0, 1 });
		}

		SECTION("empty name blocks auto-resolution")
		{
			const std::vector<CharacterView> availableCharacters = {
				MakeCharacter(1, ""),
			};

			const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
				"",
				" ",
				false,
				availableCharacters);

			REQUIRE(resolution.kind == StartupCharacterResolutionKind::NeedsPrompt);
			REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0 });
		}

		SECTION("empty name also blocks explicit selector resolution")
		{
			const std::vector<CharacterView> availableCharacters = {
				MakeCharacter(1, "MageOne"),
				MakeCharacter(2, ""),
			};

			const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
				"MageOne",
				"",
				false,
				availableCharacters);

			REQUIRE(resolution.kind == StartupCharacterResolutionKind::NeedsPrompt);
			REQUIRE(resolution.candidateIndices == std::vector<size_t>{ 0, 1 });
		}
	}
}
