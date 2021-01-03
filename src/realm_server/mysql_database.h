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
		explicit MySQLDatabase(mysql::DatabaseInfo connectionInfo);

		/// Tries to establish a connection to the MySQL server.
		bool load();

	public:
		// ~ Begin IDatabase
		std::optional<std::vector<CharacterView>> GetCharacterViewsByAccountId(uint64 accountId) final override;
		std::optional<WorldAuthData> GetWorldAuthData(std::string name) final override;
		void WorldLogin(uint64 worldId, const std::string& sessionKey, const std::string& ip, const std::string& build) final override;
		// ~ End IDatabase

	private:
		void PrintDatabaseError();

	private:
		mysql::DatabaseInfo m_connectionInfo;
		mysql::Connection m_connection;
	};
}
