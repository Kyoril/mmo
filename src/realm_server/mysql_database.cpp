// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mysql_database.h"

#include <utility>

#include "mysql_wrapper/mysql_row.h"
#include "mysql_wrapper/mysql_select.h"
#include "mysql_wrapper/mysql_statement.h"
#include "log/default_log_levels.h"
#include "game/character_flags.h"
#include "math/vector3.h"
#include "math/degree.h"


namespace mmo
{
	MySQLDatabase::MySQLDatabase(mysql::DatabaseInfo connectionInfo)
		: m_connectionInfo(std::move(connectionInfo))
	{
	}

	bool MySQLDatabase::Load()
	{
		if (!m_connection.Connect(m_connectionInfo))
		{
			ELOG("Could not connect to the realm database");
			ELOG(m_connection.GetErrorMessage());
			return false;
		}

		// Mark all realms as offline
		ILOG("Connected to MySQL at " <<
			m_connectionInfo.host << ":" <<
			m_connectionInfo.port);

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
						(flags & character_flags::Dead) != 0));

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

	std::optional<CharCreateResult> MySQLDatabase::CreateCharacter(std::string characterName, uint64 accountId, uint32 map, uint32 level, uint32 hp, uint32 gender, uint32 characterClass, uint32 race, const Vector3& position, const Degree& orientation)
	{
		if (!m_connection.Execute("INSERT INTO characters (account_id, name, map, level, race, class, gender, hp, x, y, z, o) VALUES (" +
			std::to_string(accountId) + ", '" + m_connection.EscapeString(characterName) + "', " + std::to_string(map) + ", " + std::to_string(level) + ", " +
			std::to_string(race) + ", " + std::to_string(characterClass) + ", " + std::to_string(gender) + ", " + std::to_string(hp) + ", " + std::to_string(position.x) + ", " +
			std::to_string(position.y) + ", " + std::to_string(position.z) + ", " + std::to_string(orientation.GetValueRadians()) + ");"))
		{
			PrintDatabaseError();

			if (m_connection.GetErrorCode() == 1062)
			{
				return CharCreateResult::NameAlreadyInUse;
			}

			return CharCreateResult::Error;
		}

		return CharCreateResult::Success;
	}

	std::optional<CharacterData> MySQLDatabase::CharacterEnterWorld(const uint64 characterId, const uint64 accountId)
	{
		mysql::Select select(m_connection, "SELECT name, level, map, instance, x, y, z, o FROM characters WHERE id = " + std::to_string(characterId) + " AND account_id = " + std::to_string(accountId) + " LIMIT 1");
		if (select.Success())
		{
			if (const mysql::Row row(select); row)
			{
				CharacterData result;
				result.characterId = characterId;

				String instanceId;
				uint8 level = 1;
				float facing = 0.0f;

				uint32 index = 0;
				row.GetField(index++, result.name);
				row.GetField(index++, level);
				row.GetField(index++, result.mapId);
				row.GetField(index++, instanceId);
				row.GetField(index++, result.position.x);
				row.GetField(index++, result.position.y);
				row.GetField(index++, result.position.z);
				row.GetField(index++, facing);

				result.instanceId = InstanceId::from_string(instanceId).value_or(InstanceId());
				result.facing = Radian(facing);

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

	void MySQLDatabase::PrintDatabaseError()
	{
		ELOG("Realm database error: " << m_connection.GetErrorCode() << " - " << m_connection.GetErrorMessage());
	}
}
