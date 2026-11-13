// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace
{
	/// Reads a whole text file, failing the test if it is missing.
	std::string ReadTextFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		REQUIRE(file.is_open());

		std::ostringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

	/// Tells whether any shipped UI script registers a handler for the given frame event.
	/// An event nothing registers is silently dropped, which is invisible to the player.
	bool IsEventRegisteredByUi(const std::string& event)
	{
		const std::string needle = "RegisterEvent(\"" + event + "\"";

		for (const auto& entry : std::filesystem::recursive_directory_iterator(MMO_UI_SCRIPT_DIR))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".lua")
			{
				continue;
			}

			if (ReadTextFile(entry.path().string()).find(needle) != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	/// Tells whether the shipped string table of the given locale defines the given key.
	bool LocaleHasKey(const std::string& locale, const std::string& key)
	{
		const std::string path = std::string(MMO_CLIENT_LOCALES_DIR) + "/Locale_" + locale + "/Localization.txt";
		return ReadTextFile(path).find("key = \"" + key + "\"") != std::string::npos;
	}
}

TEST_CASE("A full quest log is reported to the player", "[quests][localization]")
{
	// The world server answers an accept attempt with a full quest log by sending
	// realm_client_packet::QuestLogFull instead of accepting the quest, see
	// Player::AcceptQuestAndNotify in src/world_server/player_npc_handlers.cpp. For the
	// player to learn about it, the client has to turn that packet into an error message
	// the error frame actually shows: a frame event no shipped script registers, or a
	// string key no locale defines, both end in nothing visible on screen.
	const std::string source = ReadTextFile(std::string(MMO_CLIENT_SOURCE_DIR) + "/systems/quest_client.cpp");

	// The packet handler registration mentions the method as well, so anchor on the definition.
	const std::string::size_type handler = source.find("PacketParseResult QuestClient::OnQuestLogFull");
	REQUIRE(handler != std::string::npos);

	const std::string::size_type handlerEnd = source.find("\n\t}", handler);
	REQUIRE(handlerEnd != std::string::npos);

	const std::string body = source.substr(handler, handlerEnd - handler);

	// The handler must raise a frame event carrying a localization key, the way the other
	// client side errors do (see InventoryClient::OnInventoryChangeFailure).
	static const std::regex triggerPattern("TriggerLuaEvent\\(\\s*\"([A-Z0-9_]+)\"\\s*,\\s*\"([A-Z0-9_]+)\"\\s*\\)");
	std::smatch match;
	REQUIRE(std::regex_search(body, match, triggerPattern));

	const std::string event = match[1].str();
	const std::string key = match[2].str();
	CAPTURE(event);
	CAPTURE(key);

	CHECK(IsEventRegisteredByUi(event));

	const char* locale = GENERATE("enUS", "deDE", "frFR", "ruRU");
	CAPTURE(locale);
	CHECK(LocaleHasKey(locale, key));
}
