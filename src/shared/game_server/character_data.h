// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/radian.h"
#include "math/vector3.h"
#include "game/game.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "game/spell.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "game_server/inventory.h"
#include "game_server/quest_status_data.h"
#include "game_server/persistent_aura.h"

namespace mmo
{
	/// Per-class state for one class a character has learned. A character can know several classes
	/// (one active at a time); each carries its own class level, (reserved) class xp, attribute-point
	/// spending profile and talent ranks. See docs/multi_class_system.md.
	struct CharacterClassData
	{
		uint32 classId = 0;
		uint8 classLevel = 1;
		uint32 classXp = 0;
		std::array<uint32, 5> attributePointsSpent{};
		std::map<uint32, uint8> talentRanks;
	};

	inline io::Writer& operator<<(io::Writer& writer, const CharacterClassData& data)
	{
		writer
			<< io::write<uint32>(data.classId)
			<< io::write<uint8>(data.classLevel)
			<< io::write<uint32>(data.classXp)
			<< io::write_range(data.attributePointsSpent);

		writer << io::write<uint8>(data.talentRanks.size());
		for (const auto& [talentId, rank] : data.talentRanks)
		{
			writer << io::write<uint32>(talentId) << io::write<uint8>(rank);
		}

		return writer;
	}

	inline io::Reader& operator>>(io::Reader& reader, CharacterClassData& data)
	{
		if (!(reader
			>> io::read<uint32>(data.classId)
			>> io::read<uint8>(data.classLevel)
			>> io::read<uint32>(data.classXp)
			>> io::read_range(data.attributePointsSpent)))
		{
			return reader;
		}

		uint8 numTalents = 0;
		if (!(reader >> io::read<uint8>(numTalents)))
		{
			return reader;
		}

		data.talentRanks.clear();
		for (uint8 i = 0; i < numTalents; ++i)
		{
			uint32 talentId = 0;
			uint8 rank = 0;
			if (!(reader >> io::read<uint32>(talentId) >> io::read<uint8>(rank)))
			{
				return reader;
			}

			data.talentRanks[talentId] = rank;
		}

		return reader;
	}

	struct CharacterData
	{
		explicit CharacterData()
			: CharacterData(0, "", 0, InstanceId(), Vector3::Zero, Radian(0.0f), {}, 0, 0, 0, 1, 0, 20, 0, 0, 0, 0, Vector3::Zero, Radian(0.0f))
		{
		}

		explicit CharacterData(const ObjectId characterId, String name, const MapId mapId, const InstanceId& instanceId, const Vector3& position, const Radian& facing, const std::vector<uint32>& spellIds,
			uint32 classId, uint32 raceId, uint8 gender, uint8 level, uint32 xp, uint32 hp, uint32 mana, uint32 rage, uint32 energy, uint32 bindMap, const Vector3& bindPosition, const Radian& bindFacing)
			: characterId(characterId)
			, name(std::move(name))
			, mapId(mapId)
			, instanceId(instanceId)
			, position(position)
			, facing(facing)
			, classId(classId)
			, raceId(raceId)
			, gender(gender)
			, level(level)
			, xp(xp)
			, hp(hp)
			, mana(mana)
			, rage(rage)
			, energy(energy)
			, spellIds(spellIds)
			, bindMap(bindMap)
			, bindPosition(bindPosition)
			, bindFacing(bindFacing)
		{
		}

		ObjectId characterId;
		String name;
		MapId mapId;
		InstanceId instanceId;
		Vector3 position;
		Radian facing;
		uint32 classId;
		uint32 raceId;
		uint8 gender;
		uint8 level;
		uint32 xp;
		uint32 hp;
		uint32 maxHp = 1;	// Temp
		uint32 mana = 0;
		uint32 maxMana = 1;	// Temp
		uint32 rage = 0;
		uint32 maxRage = 100;	// Temp
		uint32 energy = 0;
		uint32 maxEnergy = 100;	// Temp
		uint8 powerType = power_type::Mana;	// Temp
		uint32 money;
		std::vector<uint32> spellIds;
		std::vector<ItemData> items;
		/// All classes this character has learned. Exactly one is active at a time (its id is in
		/// `classId`). In the current (transitional) phase a character knows only its single class.
		std::vector<CharacterClassData> knownClasses;

		std::vector<uint32> rewardedQuestIds;
		/// Daily/weekly quests that have been rewarded, mapped to the unix timestamp (seconds) at
		/// which they become available again.
		std::map<uint32, GameTime> repeatableQuestResets;
		std::map<uint32, QuestStatusData> questStatus;

		uint32 bindMap;
		Vector3 bindPosition;
		Radian bindFacing;

		uint64 groupId = 0;
		AvatarConfiguration configuration;

		uint64 guildId = 0;
		bool isGameMaster = false;

		uint32 timePlayed = 0;

		/// Persisted non-passive, non-equipment auras to restore on the next world instance.
		std::vector<PersistentAuraData> auras;
		/// Persisted spell cooldowns (as remaining milliseconds at transfer time).
		std::vector<PersistentCooldownData> cooldowns;

		/// Returns the per-class data for the currently active class, or nullptr if it is missing.
		const CharacterClassData* GetActiveClass() const
		{
			for (const auto& classData : knownClasses)
			{
				if (classData.classId == classId)
				{
					return &classData;
				}
			}

			return nullptr;
		}

		/// Returns the per-class data for the active class, creating a level-1 entry if none exists.
		CharacterClassData& GetOrCreateActiveClass()
		{
			for (auto& classData : knownClasses)
			{
				if (classData.classId == classId)
				{
					return classData;
				}
			}

			CharacterClassData classData;
			classData.classId = classId;
			classData.classLevel = 1;
			knownClasses.push_back(classData);
			return knownClasses.back();
		}
	};

	inline io::Reader& operator>>(io::Reader& reader, CharacterData& data)
	{
		if (!(reader
			>> io::read_packed_guid(data.characterId)
			>> io::read<MapId>(data.mapId)
			>> data.instanceId
			>> io::read_container<uint8>(data.name)
			>> io::read<float>(data.position.x)
			>> io::read<float>(data.position.y)
			>> io::read<float>(data.position.z)
			>> data.facing
			>> io::read_container<uint16>(data.spellIds)
			>> io::read<uint32>(data.classId)
			>> io::read<uint32>(data.raceId)
			>> io::read<uint32>(data.gender)
			>> io::read<uint8>(data.level)
			>> io::read<uint32>(data.xp)
			>> io::read<uint32>(data.hp)
			>> io::read<uint32>(data.mana)
			>> io::read<uint32>(data.rage)
			>> io::read<uint32>(data.energy)
			>> io::read<uint32>(data.money)
			>> io::read_container<uint16>(data.items)
			>> io::read<uint32>(data.bindMap)
			>> io::read<float>(data.bindPosition.x)
			>> io::read<float>(data.bindPosition.y)
			>> io::read<float>(data.bindPosition.z)
			>> data.bindFacing
			>> io::read_container<uint16>(data.rewardedQuestIds)
			>> io::read<uint64>(data.groupId)
			>> io::read<uint64>(data.guildId)
			>> data.configuration
			>> io::read<uint8>(data.isGameMaster)
			>> io::read<uint32>(data.timePlayed)))
		{
			return reader;
		}

		uint16 numQuestData;
		if (!(reader >> io::read<uint16>(numQuestData)))
		{
			return reader;
		}

		for (uint16 i = 0; i < numQuestData; ++i)
		{
			uint32 questId;
			QuestStatusData questData;
			if (!(reader >> io::read<uint32>(questId) >> questData))
			{
				return reader;
			}

			data.questStatus[questId] = questData;
		}

		uint8 numClasses = 0;
		if (!(reader >> io::read<uint8>(numClasses)))
		{
			return reader;
		}

		data.knownClasses.clear();
		data.knownClasses.reserve(numClasses);
		for (uint8 i = 0; i < numClasses; ++i)
		{
			CharacterClassData classData;
			if (!(reader >> classData))
			{
				return reader;
			}

			data.knownClasses.push_back(std::move(classData));
		}

		uint16 numRepeatableResets;
		if (!(reader >> io::read<uint16>(numRepeatableResets)))
		{
			return reader;
		}

		for (uint16 i = 0; i < numRepeatableResets; ++i)
		{
			uint32 questId;
			GameTime resetTime;
			if (!(reader >> io::read<uint32>(questId) >> io::read<uint64>(resetTime)))
			{
				return reader;
			}

			data.repeatableQuestResets[questId] = resetTime;
		}

		uint16 numAuras;
		if (!(reader >> io::read<uint16>(numAuras)))
		{
			return reader;
		}

		data.auras.clear();
		data.auras.reserve(numAuras);
		for (uint16 i = 0; i < numAuras; ++i)
		{
			PersistentAuraData aura;
			if (!(reader >> aura))
			{
				return reader;
			}

			data.auras.push_back(std::move(aura));
		}

		uint16 numCooldowns;
		if (!(reader >> io::read<uint16>(numCooldowns)))
		{
			return reader;
		}

		data.cooldowns.clear();
		data.cooldowns.reserve(numCooldowns);
		for (uint16 i = 0; i < numCooldowns; ++i)
		{
			PersistentCooldownData cooldown;
			if (!(reader >> cooldown))
			{
				return reader;
			}

			data.cooldowns.push_back(cooldown);
		}

		return reader;
	}
	
	inline io::Writer& operator<<(io::Writer& writer, const CharacterData& data)
	{
		writer
			<< io::write_packed_guid(data.characterId)
			<< io::write<MapId>(data.mapId)
			<< data.instanceId
			<< io::write_dynamic_range<uint8>(data.name)
			<< io::write<float>(data.position.x)
			<< io::write<float>(data.position.y)
			<< io::write<float>(data.position.z)
			<< data.facing
			<< io::write_dynamic_range<uint16>(data.spellIds)
			<< io::write<uint32>(data.classId)
			<< io::write<uint32>(data.raceId)
			<< io::write<uint32>(data.gender)
			<< io::write<uint8>(data.level)
			<< io::write<uint32>(data.xp)
			<< io::write<uint32>(data.hp)
			<< io::write<uint32>(data.mana)
			<< io::write<uint32>(data.rage)
			<< io::write<uint32>(data.energy)
			<< io::write<uint32>(data.money)
			<< io::write_dynamic_range<uint16>(data.items)
			<< io::write<uint32>(data.bindMap)
			<< io::write<float>(data.bindPosition.x)
			<< io::write<float>(data.bindPosition.y)
			<< io::write<float>(data.bindPosition.z)
			<< data.bindFacing
			<< io::write_dynamic_range<uint16>(data.rewardedQuestIds)
			<< io::write<uint64>(data.groupId)
			<< io::write<uint64>(data.guildId)
			<< data.configuration
			<< io::write<uint8>(data.isGameMaster)
			<< io::write<uint32>(data.timePlayed);

		writer << io::write<uint16>(data.questStatus.size());
		for (auto const& [questId, questData] : data.questStatus)
		{
			writer
				<< io::write<uint32>(questId)
				<< questData;
		}

		writer << io::write<uint8>(data.knownClasses.size());
		for (const auto& classData : data.knownClasses)
		{
			writer << classData;
		}

		writer << io::write<uint16>(data.repeatableQuestResets.size());
		for (const auto& [questId, resetTime] : data.repeatableQuestResets)
		{
			writer << io::write<uint32>(questId) << io::write<uint64>(resetTime);
		}

		writer << io::write<uint16>(data.auras.size());
		for (const auto& aura : data.auras)
		{
			writer << aura;
		}

		writer << io::write<uint16>(data.cooldowns.size());
		for (const auto& cooldown : data.cooldowns)
		{
			writer << cooldown;
		}

		return writer;
	}
}
