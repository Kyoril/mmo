// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "mysql_database.h"

#include "mysql_wrapper/mysql_row.h"
#include "mysql_wrapper/mysql_select.h"
#include "mysql_wrapper/mysql_statement.h"
#include "log/default_log_levels.h"
#include "game/character_flags.h"


namespace mmo
{
	MySQLDatabase::MySQLDatabase(const mysql::DatabaseInfo &connectionInfo)
		: m_connectionInfo(connectionInfo)
	{
	}

	bool MySQLDatabase::load()
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
				row.GetField(index++, level);
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
	
	void MySQLDatabase::PrintDatabaseError()
	{
		ELOG("Realm database error: " << m_connection.GetErrorMessage());
	}
}
