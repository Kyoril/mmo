// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
		bool Load();

	public:
		std::optional<AccountData> getAccountDataByName(std::string name) override;
		std::optional<RealmAuthData> getRealmAuthData(std::string name) override;
		std::optional<std::pair<uint64, std::string>> getAccountSessionKey(std::string accountName) override;
		void playerLogin(uint64 accountId, const std::string& sessionKey, const std::string& ip) override;
		void playerLoginFailed(uint64 accountId, const std::string& ip) override;
		void realmLogin(uint32 realmId, const std::string& sessionKey, const std::string& ip, const std::string& build) override;
		std::optional<AccountCreationResult> accountCreate(const std::string& id, const std::string& s, const std::string& v) override;
		std::optional<RealmCreationResult> realmCreate(const std::string& name, const std::string& address, uint16 port, const std::string& s, const std::string& v) override;

	private:
		void PrintDatabaseError();

	private:
		mysql::DatabaseInfo m_connectionInfo;
		mysql::Connection m_connection;
	};
}
