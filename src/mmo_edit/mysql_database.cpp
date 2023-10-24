// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mysql_database.h"
#include "mysql_wrapper/mysql_row.h"
#include "mysql_wrapper/mysql_select.h"
#include "mysql_wrapper/mysql_statement.h"
#include "log/default_log_levels.h"

namespace mmo
{
	MySQLDatabase::MySQLDatabase(const mysql::DatabaseInfo &connectionInfo)
		: m_connectionInfo(connectionInfo)
	{
	}

	bool MySQLDatabase::Load()
	{
		if (!m_connection.Connect(m_connectionInfo))
		{
			ELOG("Could not connect to the editor database");
			ELOG(m_connection.GetErrorMessage());
			return false;
		}

		// Mark all realms as offline
		ILOG("Connected to MySQL at " <<
			m_connectionInfo.host << ":" <<
			m_connectionInfo.port);

		return true;
	}

	static String GetEntityTableName(EntityType type)
	{
		switch (type)
		{
		case EntityType::Creature:
			return "creatures";
		case EntityType::Spell:
			return "spells";
		case EntityType::Item:
			return "items";
		case EntityType::Quest:
			return "quests";
		default:
			return "";
		}
	}

	std::optional<std::vector<EntityHeader>> MySQLDatabase::GetEntityList(const EntityType type)
	{
		mysql::Select select(m_connection, "SELECT id,name FROM " + GetEntityTableName(type));
		if (select.Success())
		{
			std::vector<EntityHeader> result;

			mysql::Row row(select);
			while (row)
			{
				EntityHeader data;
				row.GetField(0, data.id);
				row.GetField(1, data.name);
				result.emplace_back(data);

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

	void MySQLDatabase::PrintDatabaseError()
	{
		ELOG("Login database error: " << m_connection.GetErrorMessage());
	}
}
