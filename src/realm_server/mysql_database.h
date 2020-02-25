// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "database.h"
#include "mysql_wrapper/mysql_connection.h"


namespace mmo
{
	/// MySQL implementation of the login server database system.
	class MySQLDatabase final 
		: public IDatabase
	{
	public:
		explicit MySQLDatabase(const mysql::DatabaseInfo &connectionInfo);

		/// Tries to establish a connection to the MySQL server.
		bool load();

	private:
		void PrintDatabaseError();

	private:
		mysql::DatabaseInfo m_connectionInfo;
		mysql::Connection m_connection;
	};
}
