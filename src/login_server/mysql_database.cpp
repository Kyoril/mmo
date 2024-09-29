// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
			ELOG("Could not connect to the login database");
			ELOG(m_connection.GetErrorMessage());
			return false;
		}

		// Mark all realms as offline
		ILOG("Connected to MySQL at " <<
			m_connectionInfo.host << ":" <<
			m_connectionInfo.port);

		return true;
	}

	std::optional<AccountData> MySQLDatabase::getAccountDataByName(std::string name)
	{
		mysql::Select select(m_connection, "SELECT id,username,s,v FROM account WHERE username='" + m_connection.EscapeString(name) + "' LIMIT 1");
		if (select.Success())
		{
			mysql::Row row(select);
			if (row)
			{
				// Account exists: Get data
				AccountData data;
				row.GetField(0, data.id);
				row.GetField(1, data.name);
				row.GetField(2, data.s);
				row.GetField(3, data.v);
				return data;
			}
		}
		else
		{
			// There was an error
			PrintDatabaseError();
		}

		return {};
	}

	std::optional<RealmAuthData> MySQLDatabase::getRealmAuthData(std::string name)
	{
		mysql::Select select(m_connection, "SELECT id,name,s,v,address,port FROM realm WHERE name = '" + m_connection.EscapeString(name) + "' LIMIT 1");
		if (select.Success())
		{
			mysql::Row row(select);
			if (row)
			{
				// Create the structure and fill it with data
				RealmAuthData data;
				row.GetField(0, data.id);
				row.GetField(1, data.name);
				row.GetField(2, data.s);
				row.GetField(3, data.v);
				row.GetField(4, data.ipAddress);
				row.GetField(5, data.port);
				return data;
			}
		}
		else
		{
			// There was an error
			PrintDatabaseError();
		}

		return {};
	}

	std::optional<std::pair<uint64, std::string>> MySQLDatabase::getAccountSessionKey(std::string accountName)
	{
		mysql::Select select(m_connection, "SELECT id,k FROM account WHERE username = '" + m_connection.EscapeString(accountName) + "' LIMIT 1");
		if (select.Success())
		{
			mysql::Row row(select);
			if (row)
			{
				return std::make_pair<uint64, std::string>(std::atoll(row.GetField(0)), std::string(row.GetField(1)));
			}
		}
		else
		{
			// There was an error
			PrintDatabaseError();
		}

		return {};
	}

	void MySQLDatabase::playerLogin(const uint64 accountId, const std::string& sessionKey, const std::string& ip)
	{
		mysql::Transaction transaction(m_connection);

		if (!m_connection.Execute("UPDATE account SET k = '"
			+ m_connection.EscapeString(sessionKey)
			+ "', last_login = NOW(), last_ip = '"
			+ m_connection.EscapeString(ip)
			+ "' WHERE id = " + std::to_string(accountId)))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not update account database on login");
		}

		if (!m_connection.Execute("INSERT INTO account_login (account_id, timestamp, ip_address, succeeded) VALUES (" + 
			std::to_string(accountId) + ", NOW(), '" + ip + "', 1)"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not insert login attempt");
		}

		transaction.Commit();
	}

	void MySQLDatabase::playerLoginFailed(const uint64 accountId, const std::string& ip)
	{
		if (!m_connection.Execute("INSERT INTO account_login (account_id, timestamp, ip_address, succeeded) VALUES (" + 
			std::to_string(accountId) + ", NOW(), '" + ip + "', 0)"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not insert login attempt");
		}
	}

	void MySQLDatabase::realmLogin(const uint32 realmId, const std::string & sessionKey, const std::string & ip, const std::string & build)
	{
		if (!m_connection.Execute("UPDATE realm SET k = '"
			+ m_connection.EscapeString(sessionKey)
			+ "', last_login = NOW(), last_ip = '"
			+ m_connection.EscapeString(ip)
			+ "', last_build = '"
			+ m_connection.EscapeString(build)
			+ "' WHERE id = " + std::to_string(realmId)))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not update realm database on login");
		}
	}

	std::optional<AccountCreationResult> MySQLDatabase::accountCreate(const std::string& id, const std::string& s,
		const std::string& v)
	{
		if (!m_connection.Execute("INSERT INTO account (username, s, v) VALUES ('"
			+ m_connection.EscapeString(id) + "', '" 
			+ m_connection.EscapeString(s) + "', '" 
			+ m_connection.EscapeString(v) + "')"))
		{
			PrintDatabaseError();

			const auto errorCode = m_connection.GetErrorCode();
			if (errorCode == 1062)
			{
				return AccountCreationResult::AccountNameAlreadyInUse;
			}

			throw mysql::Exception("Could not insert account");
		}

		return AccountCreationResult::Success;
	}

	std::optional<RealmCreationResult> MySQLDatabase::realmCreate(const std::string& name, const std::string& address, uint16 port, const std::string& s, const std::string& v)
	{
		if (!m_connection.Execute("INSERT INTO realm (name, address, port, s, v) VALUES ('"
			+ m_connection.EscapeString(name) + "', '" 
			+ m_connection.EscapeString(address) + "', '" 
			+ std::to_string(port) + "', '" 
			+ m_connection.EscapeString(s) + "', '" 
			+ m_connection.EscapeString(v) 
			+ "')"))
		{
			PrintDatabaseError();

			const auto errorCode = m_connection.GetErrorCode();
			if (errorCode == 1062)
			{
				return RealmCreationResult::RealmNameAlreadyInUse;
			}

			throw mysql::Exception("Could not insert realm");
		}

		return RealmCreationResult::Success;
	}

	void MySQLDatabase::PrintDatabaseError()
	{
		ELOG("Login database error: " << m_connection.GetErrorMessage());
	}
}
