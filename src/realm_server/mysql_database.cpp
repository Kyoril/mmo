// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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
	
	void MySQLDatabase::PrintDatabaseError()
	{
		ELOG("Login database error: " << m_connection.GetErrorMessage());
	}
}
