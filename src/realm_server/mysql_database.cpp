// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "mysql_database.h"

#include <utility>

#include "mysql_wrapper/mysql_row.h"
#include "mysql_wrapper/mysql_select.h"
#include "mysql_wrapper/mysql_statement.h"
#include "log/default_log_levels.h"
#include "game/character_flags.h"
#include "game_server/inventory.h"
#include "math/vector3.h"
#include "math/degree.h"
#include "proto_data/project.h"
#include "virtual_dir/file_system_reader.h"


namespace mmo
{
	MySQLDatabase::MySQLDatabase(mysql::DatabaseInfo connectionInfo, const proto::Project& project)
		: m_project(project)
		, m_connectionInfo(std::move(connectionInfo))
	{
	}

	bool MySQLDatabase::Load()
	{
		if (!m_connection.Connect(m_connectionInfo, true))
		{
			ELOG("Could not connect to the realm database");
			ELOG(m_connection.GetErrorMessage());
			return false;
		}
		ILOG("Connected to MySQL at " << m_connectionInfo.host << ":" << m_connectionInfo.port);

		// Apply all updates
		ILOG("Checking for database updates...");

		virtual_dir::FileSystemReader reader(m_connectionInfo.updatePath);
		auto updates = reader.queryEntries("");

		// Iterate through all updates
		for (const auto& update : updates)
		{
			// Check for .sql ending in string
			if (!update.ends_with(".sql"))
			{
				continue;
			}

			// Remove ending .sql from file
			const auto updateName = update.substr(0, update.size() - 4);

			// Check if update has already been applied
			mysql::Select select(m_connection, "SELECT 1 FROM `history` WHERE `id` = '" + m_connection.EscapeString(updateName) + "' LIMIT 1;");
			if (!select.Success())
			{
				// There was an error
				PrintDatabaseError();
				return false;
			}

			if (!mysql::Row(select))
			{
				ILOG("Applying database update " << updateName << "...");

				// Row does not exist, apply update
				std::ostringstream buffer;
				auto stream = reader.readFile(update, true);

				mysql::Transaction transaction(m_connection);

				std::string line;
				while (std::getline(*stream, line))
				{
					buffer << line << "\n";
				}

				buffer << "INSERT INTO `history` (`id`) VALUES ('" << m_connection.EscapeString(updateName) << "');";

				if (!m_connection.Execute(buffer.str()))
				{
					PrintDatabaseError();
					return false;
				}

				// Drop all results
				do
				{
					if (auto* result = m_connection.StoreResult())
					{
						::mysql_free_result(result);
					}
				} while (!mysql_next_result(m_connection.GetHandle()));

				transaction.Commit();
			}
		}

		m_connection.Disconnect();

		if (!m_connection.Connect(m_connectionInfo, false))
		{
			ELOG("Could not reconnect to the realm database");
			ELOG(m_connection.GetErrorMessage());
			return false;
		}
		ILOG("Database is ready!");

		return true;
	}

	std::optional<std::vector<CharacterView>> MySQLDatabase::GetCharacterViewsByAccountId(uint64 accountId)
	{
		mysql::Select select(m_connection, "SELECT id,name,level,map,zone,race,class,gender,flags FROM characters WHERE account_id=" + std::to_string(accountId));
		if (select.Success())
		{
			std::vector<CharacterView> result;

			mysql::Row row(select);
			while (row)
			{
				uint64 guid = 0;
				std::string name;
				uint8 level = 1, gender = 0;
				uint32 map = 0, zone = 0, race = 0, charClass = 0, flags = 0;

				uint32 index = 0;
				row.GetField(index++, guid);
				row.GetField(index++, name);
				row.GetField<uint8, uint16>(index++, level);
				row.GetField(index++, map);
				row.GetField(index++, zone);
				row.GetField(index++, race);
				row.GetField(index++, charClass);
				row.GetField<uint8, uint16>(index++, gender);
				row.GetField(index++, flags);

				result.emplace_back(
					CharacterView(
						guid, 
						std::move(name), 
						level, 
						map, 
						zone, 
						race, 
						charClass, 
						gender,
						(flags & character_flags::Dead) != 0,
						0));

				// Next line entry
				row = row.Next(select);
			}

			return result;
		}
		else
		{
			// There was an error
			PrintDatabaseError();
		}

		return {};
	}

	std::optional<WorldAuthData> MySQLDatabase::GetWorldAuthData(std::string name)
	{
		mysql::Select select(m_connection, "SELECT id,name,s,v FROM world WHERE name = '" + m_connection.EscapeString(name) + "' LIMIT 1");
		if (select.Success())
		{
			mysql::Row row(select);
			if (row)
			{
				// Create the structure and fill it with data
				WorldAuthData data{};
				row.GetField(0, data.id);
				row.GetField(1, data.name);
				row.GetField(2, data.s);
				row.GetField(3, data.v);
				return std::optional<WorldAuthData>(data);
			}
		}
		else
		{
			// There was an error
			PrintDatabaseError();
		}

		return {};
	}

	void MySQLDatabase::WorldLogin(uint64 worldId, const std::string& sessionKey, const std::string& ip, const std::string& build)
	{
		if (!m_connection.Execute("UPDATE world SET k = '"
			+ m_connection.EscapeString(sessionKey)
			+ "', last_login = NOW(), last_ip = '"
			+ m_connection.EscapeString(ip)
			+ "', last_build = '"
			+ m_connection.EscapeString(build)
			+ "' WHERE id = " + std::to_string(worldId)))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not update world table on login");
		}
	}

	void MySQLDatabase::DeleteCharacter(uint64 characterGuid)
	{
		if (!m_connection.Execute("UPDATE characters SET deleted_account = account_id, account_id = NULL, deleted_at = NOW() WHERE id = " + std::to_string(characterGuid) + " AND account_id IS NOT NULL LIMIT 1;"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not update characters table");
		}
	}

	std::optional<CharCreateResult> MySQLDatabase::CreateCharacter(std::string characterName, uint64 accountId, uint32 map, uint32 level, uint32 hp, uint32 gender, uint32 race, uint32 characterClass, const Vector3& position, const Degree& orientation, std::vector<uint32> spellIds, uint32 mana, uint32 rage, uint32 energy)
	{
		if (!m_connection.Execute("INSERT INTO characters (account_id, name, map, level, race, class, gender, hp, x, y, z, o, bind_x, bind_y, bind_z, bind_o , mana, rage, energy) VALUES (" +
			std::to_string(accountId) + ", '" + m_connection.EscapeString(characterName) + "', " + std::to_string(map) + ", " + std::to_string(level) + ", " +
			std::to_string(race) + ", " + std::to_string(characterClass) + ", " + std::to_string(gender) + ", " + std::to_string(hp) + ", " + 
			std::to_string(position.x) + ", " + std::to_string(position.y) + ", " + std::to_string(position.z) + ", " + std::to_string(orientation.GetValueRadians()) + ", " + 
			std::to_string(position.x) + ", " + std::to_string(position.y) + ", " + std::to_string(position.z) + ", " + std::to_string(orientation.GetValueRadians()) + ", " + 
			std::to_string(mana) + ", " + std::to_string(rage) + ", " + std::to_string(energy) + ");"))
		{
			PrintDatabaseError();

			if (m_connection.GetErrorCode() == 1062)
			{
				return CharCreateResult::NameAlreadyInUse;
			}

			return CharCreateResult::Error;
		}

		const auto characterId = m_connection.GetLastInsertId();

		if (!spellIds.empty())
		{
			std::ostringstream queryString;
			queryString << "INSERT INTO character_spells (`character`, spell) VALUES ";

			for (const auto spellId : spellIds)
			{
				queryString << "(" << characterId << ", " << spellId << "),";
			}

			queryString.seekp(-1, std::ios_base::end);
			queryString << ";";

			// Adding spells to the character may fail but we don't cancel in this case right now
			if (!m_connection.Execute(queryString.str()))
			{
				PrintDatabaseError();
			}
		}
		
		return CharCreateResult::Success;
	}

	std::optional<CharacterData> MySQLDatabase::CharacterEnterWorld(const uint64 characterId, const uint64 accountId)
	{
		mysql::Select select(m_connection, "SELECT name, level, map, instance, x, y, z, o, gender, race, class, xp, hp, mana, rage, energy, money, bind_map, bind_x, bind_y, bind_z, bind_o, attr_0, attr_1, attr_2, attr_3, attr_4 FROM characters WHERE id = " + std::to_string(characterId) + " AND account_id = " + std::to_string(accountId) + " LIMIT 1");
		if (select.Success())
		{
			if (const mysql::Row row(select); row)
			{
				CharacterData result;
				result.characterId = characterId;

				String instanceId;
				float facing = 0.0f;

				uint32 index = 0;
				row.GetField(index++, result.name);
				row.GetField<uint8, uint16>(index++, result.level);

				// Load position and rotation
				row.GetField(index++, result.mapId);
				row.GetField(index++, instanceId);
				row.GetField(index++, result.position.x);
				row.GetField(index++, result.position.y);
				row.GetField(index++, result.position.z);
				row.GetField(index++, facing);

				// Character settings
				row.GetField<uint8, uint16>(index++, result.gender);
				row.GetField(index++, result.raceId);
				row.GetField(index++, result.classId);

				// Character state (attributes)
				row.GetField(index++, result.xp);
				row.GetField(index++, result.hp);
				row.GetField(index++, result.mana);
				row.GetField(index++, result.rage);
				row.GetField(index++, result.energy);
				row.GetField(index++, result.money);

				// Load bind position and rotation
				row.GetField(index++, result.bindMap);
				row.GetField(index++, result.bindPosition.x);
				row.GetField(index++, result.bindPosition.y);
				row.GetField(index++, result.bindPosition.z);
				float bindFacing = 0.0f;
				row.GetField(index++, bindFacing);

				// Load attribute points spent
				row.GetField(index++, result.attributePointsSpent[0]);
				row.GetField(index++, result.attributePointsSpent[1]);
				row.GetField(index++, result.attributePointsSpent[2]);
				row.GetField(index++, result.attributePointsSpent[3]);
				row.GetField(index++, result.attributePointsSpent[4]);

				result.instanceId = InstanceId::from_string(instanceId).value_or(InstanceId());
				result.facing = Radian(facing);
				result.bindFacing = Radian(bindFacing);

				// Load character spell ids
				if(mysql::Select spellSelect(m_connection, "SELECT spell FROM character_spells WHERE `character` = " + std::to_string(characterId)); spellSelect.Success())
				{
					mysql::Row spellRow(spellSelect);
					while (spellRow)
					{
						uint32 spellId = 0;
						spellRow.GetField(0, spellId);
						result.spellIds.push_back(spellId);
						spellRow = mysql::Row::Next(spellSelect);
					}
				}
				else
				{
					PrintDatabaseError();
					throw mysql::Exception("Could not load character spells");
				}

				// Load item data
				mysql::Select itemSelect(m_connection, 
					"SELECT `entry`, `slot`, `creator`, `count`, `durability` FROM `character_items` WHERE `owner`=" + std::to_string(characterId));
				if (itemSelect.Success())
				{
					mysql::Row itemRow(itemSelect);
					while (itemRow)
					{
						// Read item data
						ItemData data;
						itemRow.GetField(0, data.entry);
						itemRow.GetField(1, data.slot);
						itemRow.GetField(2, data.creator);
						itemRow.GetField<uint8, uint16>(3, data.stackCount);
						itemRow.GetField(4, data.durability);
						if (const auto* itemEntry = m_project.items.getById(data.entry))
						{
							// More than 15 minutes passed since last save?
							if (const bool isConjured = (itemEntry->flags() & item_flags::Conjured) != 0; !isConjured)
							{
								result.items.emplace_back(std::move(data));
							}
						}
						else
						{
							WLOG("Unknown item in character database: " << data.entry);
						}

						// Next row
						itemRow = mmo::mysql::Row::Next(itemSelect);
					}
				}

				return result;
			}
		}
		else
		{
			// There was an error
			PrintDatabaseError();
		}

		return {};
	}

	std::optional<WorldCreationResult> MySQLDatabase::CreateWorkd(const String& name, const String& s, const String& v)
	{
		if (!m_connection.Execute("INSERT INTO world (name, s, v) VALUES ('"
			+ m_connection.EscapeString(name) + "', '"
			+ m_connection.EscapeString(s) + "', '"
			+ m_connection.EscapeString(v) + "')"))
		{
			PrintDatabaseError();

			const auto errorCode = m_connection.GetErrorCode();
			if (errorCode == 1062)
			{
				return WorldCreationResult::WorldNameAlreadyInUse;
			}

			throw mysql::Exception("Could not create world");
		}

		return WorldCreationResult::Success;
	}

	void MySQLDatabase::ChatMessage(const uint64 characterId, const uint16 type, const String message)
	{
		if (!m_connection.Execute("INSERT INTO character_chat (`character`, `type`, `message`, `timestamp`) VALUES ("
			+ std::to_string(characterId) + ", "
			+ std::to_string(type) + ", '"
			+ m_connection.EscapeString(message) + "', NOW())"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not save chat message to database");
		}
	}

	void MySQLDatabase::UpdateCharacter(uint64 characterId, uint32 map, const Vector3& position,
		const Radian& orientation, uint32 level, uint32 xp, uint32 hp, uint32 mana, uint32 rage, uint32 energy, uint32 money, const std::vector<ItemData>& items,
		uint32 bindMap, const Vector3& bindPosition, const Radian& bindFacing, std::array<uint32, 5> attributePointsSpent)
	{
		if (!m_connection.Execute(std::string("UPDATE characters SET ")
			+ "map = '" + std::to_string(map) + "'"
			+ ", level = '" + std::to_string(level) + "'"
			+ ", x = '" + std::to_string(position.x) + "'"
			+ ", y = '" + std::to_string(position.y) + "'"
			+ ", z = '" + std::to_string(position.z) + "'"
			+ ", o = '" + std::to_string(orientation.GetValueRadians()) + "'"
			+ ", xp = " + std::to_string(xp)
			+ ", hp = " + std::to_string(hp)
			+ ", mana = " + std::to_string(mana)
			+ ", rage = " + std::to_string(rage)
			+ ", energy = " + std::to_string(energy)
			+ ", money = " + std::to_string(money)
			+ ", bind_map = " + std::to_string(bindMap)
			+ ", bind_x = " + std::to_string(bindPosition.x)
			+ ", bind_y = " + std::to_string(bindPosition.y)
			+ ", bind_z = " + std::to_string(bindPosition.z)
			+ ", bind_o = " + std::to_string(bindFacing.GetValueRadians())
			+ ", attr_0 = " + std::to_string(attributePointsSpent[0])
			+ ", attr_1 = " + std::to_string(attributePointsSpent[1])
			+ ", attr_2 = " + std::to_string(attributePointsSpent[2])
			+ ", attr_3 = " + std::to_string(attributePointsSpent[3])
			+ ", attr_4 = " + std::to_string(attributePointsSpent[4])
			+ " WHERE id = '" + std::to_string(characterId) + "'"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not update character data!");
		}

		// Delete character items
		if (!m_connection.Execute("DELETE FROM `character_items` WHERE `owner`=" + std::to_string(characterId) + ";"))
		{
			// There was an error
			PrintDatabaseError();
			throw mysql::Exception("Could not update character inventory data!");
		}

		// Save character items
		if (!items.empty())
		{
			std::ostringstream strm;
			strm << "INSERT INTO `character_items` (`owner`, `entry`, `slot`, `creator`, `count`, `durability`) VALUES ";
			bool isFirstItem = true;
			for (auto& item : items)
			{
				// Don't save buyback slots into the database!
				if (Inventory::IsBuyBackSlot(item.slot))
					continue;

				if (!isFirstItem) strm << ",";
				else
				{
					isFirstItem = false;
				}

				strm << "(" << characterId << "," << item.entry << "," << item.slot << ",";
				if (item.creator == 0)
				{
					strm << "NULL";
				}
				else
				{
					strm << item.creator;
				}
				strm << "," << static_cast<uint16>(item.stackCount) << "," << item.durability << ")";
			}
			strm << ";";

			if (!m_connection.Execute(strm.str()))
			{
				// There was an error
				PrintDatabaseError();
				throw mysql::Exception("Could not update character inventory data!");
			}
		}

	}

	void MySQLDatabase::PrintDatabaseError()
	{
		ELOG("Realm database error: " << m_connection.GetErrorCode() << " - " << m_connection.GetErrorMessage());
	}
}
