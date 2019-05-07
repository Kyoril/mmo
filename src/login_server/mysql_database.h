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

	public:
		std::optional<AccountData> getAccountDataByName(const std::string& name) override;
		std::optional<RealmAuthData> getRealmAuthData(uint32 realmId) override;
		std::optional<std::pair<uint64, std::string>> getAccountSessionKey(const std::string& accountName) override;
		void playerLogin(uint64 accountId, const std::string& sessionKey, const std::string& ip) override;

	private:
		void PrintDatabaseError();

	private:
		mysql::DatabaseInfo m_connectionInfo;
		mysql::Connection m_connection;
	};
}
