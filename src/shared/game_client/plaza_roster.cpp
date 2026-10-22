// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "plaza_roster.h"

#include <algorithm>
#include <cstdint>

namespace mmo
{
	bool PlazaAppearance::operator==(const PlazaAppearance& other) const
	{
		return displayId == other.displayId &&
			raceId == other.raceId &&
			gender == other.gender &&
			equipmentDisplayIds == other.equipmentDisplayIds &&
			chosenOptionPerGroup == other.chosenOptionPerGroup &&
			scalarValues == other.scalarValues;
	}

	PlazaAppearance MakePlazaAppearance(const CharacterView& view)
	{
		PlazaAppearance appearance;
		appearance.displayId = view.GetDisplayId();
		appearance.raceId = view.GetRaceId();
		appearance.gender = view.GetGender();
		appearance.chosenOptionPerGroup = view.GetConfiguration().chosenOptionPerGroup;
		appearance.scalarValues = view.GetConfiguration().scalarValues;
		appearance.equipmentDisplayIds = view.GetEquipmentDisplayIds();
		return appearance;
	}

	namespace
	{
		bool parseDecimal(const String& text, uint64& out)
		{
			if (text.empty() || text.size() > 20)
			{
				return false;
			}

			uint64 value = 0;
			for (const char c : text)
			{
				if (c < '0' || c > '9')
				{
					return false;
				}

				const uint64 digit = static_cast<uint64>(c - '0');
				if (value > (UINT64_MAX - digit) / 10)
				{
					return false;
				}
				value = value * 10 + digit;
			}

			out = value;
			return true;
		}

		/// Splits the value into valid realm/guid pairs, dropping malformed entries.
		std::vector<std::pair<uint64, uint64>> parseEntries(const String& value)
		{
			std::vector<std::pair<uint64, uint64>> result;

			size_t start = 0;
			while (start <= value.size())
			{
				size_t end = value.find(';', start);
				if (end == String::npos)
				{
					end = value.size();
				}

				const String entry = value.substr(start, end - start);
				const size_t colon = entry.find(':');
				if (colon != String::npos)
				{
					uint64 realm = 0, guid = 0;
					if (parseDecimal(entry.substr(0, colon), realm) && parseDecimal(entry.substr(colon + 1), guid))
					{
						result.emplace_back(realm, guid);
					}
				}

				start = end + 1;
			}

			return result;
		}
	}

	PlazaAssignment AssignPlaza(int32 numCharacters, int32 selectedIndex)
	{
		PlazaAssignment result;
		result.spotCharacter.fill(-1);
		result.frontCharacter = -1;
		result.frontIsVisitor = false;

		if (numCharacters <= 0)
		{
			return result;
		}

		for (int32 i = 0; i < PlazaSpotCount && i < numCharacters; ++i)
		{
			result.spotCharacter[i] = i;
		}

		if (selectedIndex >= 0 && selectedIndex < numCharacters)
		{
			result.frontCharacter = selectedIndex;
			if (selectedIndex < PlazaSpotCount)
			{
				result.spotCharacter[selectedIndex] = -1;
			}
			else
			{
				result.frontIsVisitor = true;
			}
		}

		return result;
	}

	int32 ResolveInitialSelection(const std::vector<uint64>& orderedGuids, uint64 lastPlayedGuid)
	{
		if (orderedGuids.empty())
		{
			return -1;
		}

		const auto it = std::find(orderedGuids.begin(), orderedGuids.end(), lastPlayedGuid);
		return it == orderedGuids.end() ? 0 : static_cast<int32>(it - orderedGuids.begin());
	}

	void MoveRosterEntry(std::vector<uint64>& order, size_t from, size_t to)
	{
		if (from >= order.size() || to >= order.size() || from == to)
		{
			return;
		}

		const uint64 value = order[from];
		order.erase(order.begin() + static_cast<std::ptrdiff_t>(from));
		order.insert(order.begin() + static_cast<std::ptrdiff_t>(to), value);
	}

	uint64 GetLastCharacterForRealm(const String& cvarValue, uint32 realmId)
	{
		for (const auto& entry : parseEntries(cvarValue))
		{
			if (entry.first == realmId)
			{
				return entry.second;
			}
		}

		return 0;
	}

	String SetLastCharacterForRealm(const String& cvarValue, uint32 realmId, uint64 guid)
	{
		auto entries = parseEntries(cvarValue);

		bool found = false;
		for (auto& entry : entries)
		{
			if (entry.first == realmId)
			{
				entry.second = guid;
				found = true;
			}
		}

		if (!found)
		{
			entries.emplace_back(realmId, guid);
		}

		String result;
		for (const auto& entry : entries)
		{
			if (!result.empty())
			{
				result += ';';
			}
			result += std::to_string(entry.first) + ":" + std::to_string(entry.second);
		}

		return result;
	}

	GlueArrival DecideGlueArrival(bool plazaReady, bool returningFromWorld)
	{
		// Back from the world the player has seen the flight already: always a cut. A fresh login
		// always flies, even when the list beats the scene (common with auto-login): the fade
		// stays up until the scene is ready, so the gate and the flight are never skipped.
		if (returningFromWorld)
		{
			return plazaReady ? GlueArrival::Snap : GlueArrival::SnapWhenReady;
		}

		return plazaReady ? GlueArrival::FlyIn : GlueArrival::FlyInWhenReady;
	}
}
