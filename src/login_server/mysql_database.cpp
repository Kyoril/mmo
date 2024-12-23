// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mysql_database.h"
#include "mysql_wrapper/mysql_row.h"
#include "mysql_wrapper/mysql_select.h"
#include "mysql_wrapper/mysql_statement.h"
#include "log/default_log_levels.h"

#include "virtual_dir/file_system_reader.h"

namespace mmo
{
	MySQLDatabase::MySQLDatabase(const mysql::DatabaseInfo &connectionInfo)
		: m_connectionInfo(connectionInfo)
	{
	}

	bool MySQLDatabase::Load()
	{
		if (!m_connection.Connect(m_connectionInfo, true))
		{
			ELOG("Could not connect to the login database");
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

		// Disconnect
		m_connection.Disconnect();

		// Reconnect without multi query for security reasons
		if (!m_connection.Connect(m_connectionInfo, false))
		{
			ELOG("Could not reconnect to the login database");
			ELOG(m_connection.GetErrorMessage());
			return false;
		}

		ILOG("Database is ready!");

		return true;
	}

	std::optional<AccountData> MySQLDatabase::getAccountDataByName(std::string name)
	{
		mysql::Select select(m_connection, "SELECT id,username,s,v,"
			"CASE "
			"WHEN banned = 1 AND (ban_expiration IS NULL) THEN 2 "
			"WHEN banned = 1 AND (ban_expiration >= NOW()) THEN 1 "
			"ELSE 0 "
			"END AS ban_state "
			"FROM account WHERE username='" + m_connection.EscapeString(name) + "' LIMIT 1");
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
				row.GetField<BanState, uint32>(4, data.banned);
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

	void MySQLDatabase::banAccountByName(const std::string& accountName, const std::string& expiration, const std::string& reason)
	{
		mysql::Transaction transaction(m_connection);

		std::ostringstream query;
		query << "UPDATE `account` SET `banned` = 1";

		if (!expiration.empty())
		{
			query << ", `ban_expiration` = '" << m_connection.EscapeString(expiration) << "'";
		}

		query << " WHERE `username` = '" << m_connection.EscapeString(accountName) << "' LIMIT 1";

		if (!m_connection.Execute(query.str()))
		{
			PrintDatabaseError();
			throw mysql::Exception("Failed to ban account " + accountName);
		}

		const String expirationValue = expiration.empty() ? "NULL" : "'" + m_connection.EscapeString(expiration) + "'";
		const String reasonValue = reason.empty() ? "NULL" : "'" + m_connection.EscapeString(reason) + "'";

		if (!m_connection.Execute("INSERT INTO `account_ban_history` (`account_id`, `banned`, `expiration`, `reason`) SELECT `id`, 1, " + expirationValue + ", " + reasonValue + " FROM `account` WHERE `username` = '" + m_connection.EscapeString(accountName) + "' LIMIT 1"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Failed to ban account " + accountName);
		}

		transaction.Commit();
	}

	void MySQLDatabase::unbanAccountByName(const std::string& accountName, const std::string& reason)
	{
		mysql::Transaction transaction(m_connection);

		if (!m_connection.Execute("UPDATE `account` SET `banned` = 0, `ban_expiration` = NULL WHERE `username` = '" + m_connection.EscapeString(accountName) + "' LIMIT 1"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Failed to unban account " + accountName);
		}

		const String reasonValue = reason.empty() ? "NULL" : "'" + m_connection.EscapeString(reason) + "'";

		if (!m_connection.Execute("INSERT INTO `account_ban_history` (`account_id`, `banned`, `expiration`, `reason`) SELECT `id`, 0, NULL, " + reasonValue + " FROM `account` WHERE `username` = '" + m_connection.EscapeString(accountName) + "' LIMIT 1"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Failed to ban account " + accountName);
		}

		transaction.Commit();
	}

	void MySQLDatabase::PrintDatabaseError()
	{
		ELOG("Login database error: " << m_connection.GetErrorMessage());
	}
}
