// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_roster.h"

#include "log/default_log_levels.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <unordered_set>

namespace mmo
{
	namespace
	{
		const char* const g_nameStarts[] = {
			"Ael", "Bran", "Cor", "Dara", "Eld", "Fen", "Gor", "Hal", "Ith", "Jor",
			"Kel", "Lor", "Mar", "Nym", "Orl", "Pri", "Quin", "Rav", "Syl", "Tor",
			"Ulf", "Vay", "Wyn", "Xan", "Yri", "Zar"
		};

		const char* const g_nameMiddles[] = {
			"a", "e", "i", "o", "u", "ae", "ia", "or", "an", "el", "ur", "in"
		};

		const char* const g_nameEnds[] = {
			"ric", "dor", "wyn", "las", "mir", "gar", "thas", "nor", "vale", "dan",
			"sha", "ren", "lyn", "kar", "beth", "rin"
		};

		template <typename T, std::size_t N>
		const char* pick(const T (&table)[N], const uint32 value)
		{
			return table[value % N];
		}

		/// Splitmix-style mixing so that neighbouring indices produce names that do not look
		/// related. A swarm whose bots are called Aeldor1 through Aeldor50 reads as a swarm.
		uint32 mix(uint32 value)
		{
			value ^= value >> 16;
			value *= 0x7feb352du;
			value ^= value >> 15;
			value *= 0x846ca68bu;
			value ^= value >> 16;
			return value;
		}
	}

	std::string GenerateBotName(const uint32 seed)
	{
		const uint32 a = mix(seed);
		const uint32 b = mix(seed ^ 0x9e3779b9u);
		const uint32 c = mix(seed + 0x85ebca6bu);

		std::string name = pick(g_nameStarts, a);

		// A middle syllable only sometimes, so names vary in length rather than all being three
		// syllables long.
		if ((b & 1u) != 0)
		{
			name += pick(g_nameMiddles, b >> 1);
		}

		name += pick(g_nameEnds, c);

		// The realm accepts 3 to 12 letters and nothing else. The tables only contain letters,
		// so the only thing that can go wrong is length.
		if (name.size() > 12)
		{
			name.resize(12);
		}

		name[0] = static_cast<char>(::toupper(static_cast<unsigned char>(name[0])));
		return name;
	}

	std::vector<BotRosterEntry> BuildRoster(
		const uint32 count,
		const std::string& accountPrefix,
		const std::string& password,
		const uint32 minLevel,
		const uint32 maxLevel,
		const uint32 classCount)
	{
		std::vector<BotRosterEntry> roster;
		roster.reserve(count);

		std::unordered_set<std::string> usedNames;

		for (uint32 index = 0; index < count; ++index)
		{
			BotRosterEntry entry;
			entry.index = index;
			entry.account = accountPrefix + std::to_string(index);
			entry.password = password;
			entry.seed = mix(index ^ 0x5bf03635u);

			// Names collide occasionally because the syllable tables are small. Folding an
			// attempt counter into the seed re-rolls without giving up determinism.
			uint32 attempt = 0;
			do
			{
				entry.characterName = GenerateBotName(index + attempt * 7919u);
				++attempt;
			} while (!usedNames.insert(entry.characterName).second && attempt < 64);

			entry.characterClass = static_cast<uint8>(classCount == 0 ? 1 : (index % classCount));
			entry.gender = static_cast<uint8>((entry.seed >> 3) & 1u);
			entry.race = 0;

			// Level distribution is deliberately not uniform: a tenth of the swarm sits at each
			// end of the range so that the lowest and highest content is always being played,
			// rather than being the thin tails of a uniform roll.
			std::mt19937 random(entry.seed);
			const uint32 roll = std::uniform_int_distribution<uint32>(0, 99)(random);
			if (roll < 10)
			{
				entry.targetLevel = minLevel;
			}
			else if (roll < 20)
			{
				entry.targetLevel = maxLevel;
			}
			else
			{
				entry.targetLevel = std::uniform_int_distribution<uint32>(minLevel, maxLevel)(random);
			}

			roster.push_back(std::move(entry));
		}

		return roster;
	}

	bool LoadRoster(const std::string& path, std::vector<BotRosterEntry>& outRoster)
	{
		std::ifstream file(path);
		if (!file)
		{
			return false;
		}

		nlohmann::json data;
		file >> data;

		if (!data.is_array())
		{
			ELOG("Roster file " << path << " is not an array");
			return false;
		}

		outRoster.clear();
		outRoster.reserve(data.size());

		for (const auto& item : data)
		{
			BotRosterEntry entry;
			entry.index = item.value("index", 0u);
			entry.account = item.value("account", std::string());
			entry.password = item.value("password", std::string());
			entry.characterName = item.value("characterName", std::string());
			entry.race = static_cast<uint8>(item.value("race", 0u));
			entry.characterClass = static_cast<uint8>(item.value("class", 1u));
			entry.gender = static_cast<uint8>(item.value("gender", 0u));
			entry.targetLevel = item.value("targetLevel", 1u);
			entry.seed = item.value("seed", 0u);

			if (entry.account.empty() || entry.characterName.empty())
			{
				ELOG("Roster entry " << entry.index << " is missing an account or character name");
				return false;
			}

			outRoster.push_back(std::move(entry));
		}

		return !outRoster.empty();
	}

	bool SaveRoster(const std::string& path, const std::vector<BotRosterEntry>& roster)
	{
		nlohmann::json data = nlohmann::json::array();
		for (const BotRosterEntry& entry : roster)
		{
			nlohmann::json item;
			item["index"] = entry.index;
			item["account"] = entry.account;
			item["password"] = entry.password;
			item["characterName"] = entry.characterName;
			item["race"] = entry.race;
			item["class"] = entry.characterClass;
			item["gender"] = entry.gender;
			item["targetLevel"] = entry.targetLevel;
			item["seed"] = entry.seed;
			data.push_back(std::move(item));
		}

		std::ofstream file(path, std::ios::out | std::ios::trunc);
		if (!file)
		{
			ELOG("Failed to write roster to " << path);
			return false;
		}

		file << data.dump(2) << "\n";
		return true;
	}
}
