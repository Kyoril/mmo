// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mysql_database.h"

#include "base/timer_queue.h"
#include "mysql_wrapper/mysql_row.h"
#include "mysql_wrapper/mysql_select.h"
#include "mysql_wrapper/mysql_statement.h"
#include "log/default_log_levels.h"

#include "virtual_dir/file_system_reader.h"

namespace mmo
{
	MySQLDatabase::MySQLDatabase(mysql::DatabaseInfo connectionInfo, TimerQueue& timerQueue, WorkerDispatcher dbWorker)
		: m_connectionInfo(std::move(connectionInfo))
		, m_timerQueue(timerQueue)
		, m_dbWorker(std::move(dbWorker))
		, m_pingCountdown(timerQueue)
	{
		m_pingConnection = m_pingCountdown.ended += [this]()
			{
				// The ping timer fires on the IO thread, but the MySQL connection is otherwise only
				// ever touched on the database worker thread. Run the keep-alive there too so the two
				// never race (which would surface as "Lost connection to MySQL server during query").
				m_dbWorker([this]()
					{
						std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);
						if (!m_connection.KeepAlive())
						{
							ELOG("MySQL ping failed: " << m_connection.GetErrorMessage());
						}
					});

				SetNextPingTimer();
			};
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

		SetNextPingTimer();

		ILOG("Database is ready!");

		return true;
	}

	void MySQLDatabase::SetNextPingTimer() const
	{
		// Ping every 30 seconds
		m_pingCountdown.SetEnd(GetAsyncTimeMs() + 30000);
	}

	std::optional<AccountData> MySQLDatabase::GetAccountDataByName(std::string name)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

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

	std::optional<RealmAuthData> MySQLDatabase::GetRealmAuthData(std::string name)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

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

	std::optional<std::tuple<uint64, std::string, uint8>> MySQLDatabase::GetAccountSessionKey(std::string accountName)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		mysql::Select select(m_connection, "SELECT id, k, gm_level FROM account WHERE username = '" + m_connection.EscapeString(accountName) + "' LIMIT 1");
		if (select.Success())
		{
			mysql::Row row(select);
			if (row)
			{
				return std::make_tuple<uint64, std::string, uint8>(
					std::atoll(row.GetField(0)), 
					std::string(row.GetField(1)), 
					static_cast<uint8>(std::atoi(row.GetField(2))));
			}
		}
		else
		{
			// There was an error
			PrintDatabaseError();
		}

		return {};
	}

	void MySQLDatabase::PlayerLogin(const uint64 accountId, const std::string& sessionKey, const std::string& ip)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

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

	void MySQLDatabase::PlayerLoginFailed(const uint64 accountId, const std::string& ip)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		if (!m_connection.Execute("INSERT INTO account_login (account_id, timestamp, ip_address, succeeded) VALUES (" + 
			std::to_string(accountId) + ", NOW(), '" + ip + "', 0)"))
		{
			PrintDatabaseError();
			throw mysql::Exception("Could not insert login attempt");
		}
	}

	void MySQLDatabase::RealmLogin(const uint32 realmId, const std::string & sessionKey, const std::string & ip, const std::string & build)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

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

	std::optional<AccountCreationResult> MySQLDatabase::AccountCreate(const std::string& id, const std::string& s,
		const std::string& v)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

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

	std::optional<RealmCreationResult> MySQLDatabase::RealmCreate(const std::string& name, const std::string& address, uint16 port, const std::string& s, const std::string& v)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

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

	void MySQLDatabase::BanAccountByName(const std::string& accountName, const std::string& expiration, const std::string& reason)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

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

	void MySQLDatabase::UnbanAccountByName(const std::string& accountName, const std::string& reason)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

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

	bool MySQLDatabase::SetAccountGMLevel(std::string accountName, uint8 gmLevel)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		try
		{
			// The correct way to execute a query is to use m_connection.Execute() directly
			const std::string query = "UPDATE account SET gm_level = " + std::to_string(gmLevel) + 
				" WHERE username = '" + m_connection.EscapeString(accountName) + "' LIMIT 1";
				
			if (!m_connection.Execute(query))
			{
				PrintDatabaseError();
				return false;
			}
			
			// Check if any row was actually updated
			return mysql_affected_rows(m_connection.GetHandle()) > 0;
		}
		catch (const mysql::Exception& ex)
		{
			ELOG("Database error when setting GM level: " << ex.what());
			return false;
		}
	}

	AccountListResult MySQLDatabase::GetAccountList(const AccountListParams& params)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		AccountListResult result;

		// Whitelist sort columns to prevent SQL injection via the sort parameter.
		const std::string sortColumn = [&]() -> std::string
		{
			if (params.sortBy == "username") return "a.username";
			if (params.sortBy == "last_login") return "a.last_login";
			if (params.sortBy == "created_at") return "a.created_at";
			return "a.id";
		}();
		const std::string sortDir = params.sortAsc ? "ASC" : "DESC";

		// Build WHERE clause additions.
		std::string where;

		if (!params.search.empty())
		{
			const std::string escaped = m_connection.EscapeString(params.search);
			if (params.searchField == "email")
			{
				where += " AND ai.email LIKE '%" + escaped + "%'";
			}
			else if (params.searchField == "id")
			{
				where += " AND a.id = " + std::to_string(std::strtoull(params.search.c_str(), nullptr, 10));
			}
			else
			{
				where += " AND a.username LIKE '%" + escaped + "%'";
			}
		}

		if (params.bannedOnly)
		{
			where += " AND (a.banned = 1 AND (a.ban_expiration IS NULL OR a.ban_expiration >= NOW()))";
		}

		// Count query.
		const std::string countSql =
			"SELECT COUNT(*) FROM `account` a "
			"LEFT JOIN `account_info` ai ON ai.account = a.id "
			"WHERE 1=1" + where;

		{
			mysql::Select countSelect(m_connection, countSql);
			if (!countSelect.Success())
			{
				PrintDatabaseError();
				return result;
			}
			mysql::Row row(countSelect);
			if (row)
			{
				result.total = std::strtoull(row.GetField(0), nullptr, 10);
			}
		}

		// Data query.
		const uint32 offset = (params.page > 0 ? params.page - 1 : 0) * params.limit;
		const std::string dataSql =
			"SELECT a.id, a.username, "
			"IFNULL(ai.email,'') AS email, "
			"DATE_FORMAT(a.created_at,'%Y-%m-%dT%H:%i:%sZ') AS created_at, "
			"IFNULL(DATE_FORMAT(a.last_login,'%Y-%m-%dT%H:%i:%sZ'),'') AS last_login, "
			"IFNULL(a.last_ip,'') AS last_ip, "
			"IFNULL(a.gm_level,0) AS gm_level, "
			"CASE "
			"  WHEN a.banned=1 AND a.ban_expiration IS NULL THEN 2 "
			"  WHEN a.banned=1 AND a.ban_expiration>=NOW() THEN 1 "
			"  ELSE 0 "
			"END AS ban_state, "
			"IFNULL(DATE_FORMAT(a.ban_expiration,'%Y-%m-%dT%H:%i:%sZ'),'') AS ban_expiration "
			"FROM `account` a "
			"LEFT JOIN `account_info` ai ON ai.account = a.id "
			"WHERE 1=1" + where +
			" ORDER BY " + sortColumn + " " + sortDir +
			" LIMIT " + std::to_string(params.limit) +
			" OFFSET " + std::to_string(offset);

		mysql::Select dataSelect(m_connection, dataSql);
		if (!dataSelect.Success())
		{
			PrintDatabaseError();
			return result;
		}

		mysql::Row row(dataSelect);
		while (row)
		{
			AccountListEntry entry;
			entry.id = std::strtoull(row.GetField(0), nullptr, 10);
			entry.username = row.GetField(1) ? row.GetField(1) : "";
			entry.email = row.GetField(2) ? row.GetField(2) : "";
			entry.created_at = row.GetField(3) ? row.GetField(3) : "";
			entry.last_login = row.GetField(4) ? row.GetField(4) : "";
			entry.last_ip = row.GetField(5) ? row.GetField(5) : "";
			entry.gm_level = static_cast<uint8>(std::atoi(row.GetField(6)));
			entry.ban_state = static_cast<uint8>(std::atoi(row.GetField(7)));
			entry.ban_expiration = row.GetField(8) ? row.GetField(8) : "";
			result.accounts.push_back(std::move(entry));
			row = mysql::Row(dataSelect);
		}

		return result;
	}

	std::vector<RealmListEntry> MySQLDatabase::GetRealmList()
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		std::vector<RealmListEntry> realms;

		mysql::Select select(m_connection,
			"SELECT id, name, address, port, "
			"IFNULL(DATE_FORMAT(last_login,'%Y-%m-%dT%H:%i:%sZ'),'') AS last_login, "
			"IFNULL(last_ip,'') AS last_ip, "
			"IFNULL(last_build,'') AS last_build "
			"FROM `realm` ORDER BY id");

		if (!select.Success())
		{
			PrintDatabaseError();
			return realms;
		}

		mysql::Row row(select);
		while (row)
		{
			RealmListEntry entry;
			entry.id = static_cast<uint32>(std::atoi(row.GetField(0)));
			entry.name = row.GetField(1) ? row.GetField(1) : "";
			entry.address = row.GetField(2) ? row.GetField(2) : "";
			entry.port = static_cast<uint16>(std::atoi(row.GetField(3)));
			entry.last_login = row.GetField(4) ? row.GetField(4) : "";
			entry.last_ip = row.GetField(5) ? row.GetField(5) : "";
			entry.last_build = row.GetField(6) ? row.GetField(6) : "";
			entry.is_online = false;  // Filled in by the HTTP handler via RealmManager.
			realms.push_back(std::move(entry));
			row = mysql::Row(select);
		}

		return realms;
	}

	std::vector<FeatureDefinition> MySQLDatabase::GetFeatures()
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		std::vector<FeatureDefinition> features;

		mysql::Select select(m_connection,
			"SELECT id, name, IFNULL(description,''), "
			"IFNULL(DATE_FORMAT(created_at,'%Y-%m-%dT%H:%i:%sZ'),'') "
			"FROM `feature` ORDER BY name");
		if (!select.Success())
		{
			PrintDatabaseError();
			return features;
		}

		mysql::Row row(select);
		while (row)
		{
			FeatureDefinition entry;
			entry.id = static_cast<uint32>(std::strtoul(row.GetField(0), nullptr, 10));
			entry.name = row.GetField(1) ? row.GetField(1) : "";
			entry.description = row.GetField(2) ? row.GetField(2) : "";
			entry.created_at = row.GetField(3) ? row.GetField(3) : "";
			features.push_back(std::move(entry));
			row = mysql::Row(select);
		}

		return features;
	}

	std::optional<uint32> MySQLDatabase::CreateFeature(const std::string& name, const std::string& description)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		const std::string descValue = description.empty() ? "NULL" : "'" + m_connection.EscapeString(description) + "'";

		if (!m_connection.Execute("INSERT INTO `feature` (name, description) VALUES ('"
			+ m_connection.EscapeString(name) + "', " + descValue + ")"))
		{
			const auto errorCode = m_connection.GetErrorCode();
			if (errorCode == 1062)
			{
				// Name already in use
				return {};
			}

			PrintDatabaseError();
			throw mysql::Exception("Could not insert feature");
		}

		return static_cast<uint32>(mysql_insert_id(m_connection.GetHandle()));
	}

	bool MySQLDatabase::DeleteFeature(uint32 featureId)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		if (!m_connection.Execute("DELETE FROM `feature` WHERE id = " + std::to_string(featureId) + " LIMIT 1"))
		{
			PrintDatabaseError();
			return false;
		}

		return mysql_affected_rows(m_connection.GetHandle()) > 0;
	}

	std::optional<uint32> MySQLDatabase::GetFeatureIdByName(const std::string& name)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		mysql::Select select(m_connection, "SELECT id FROM `feature` WHERE name = '" + m_connection.EscapeString(name) + "' LIMIT 1");
		if (select.Success())
		{
			mysql::Row row(select);
			if (row)
			{
				return static_cast<uint32>(std::strtoul(row.GetField(0), nullptr, 10));
			}
		}
		else
		{
			PrintDatabaseError();
		}

		return {};
	}

	std::optional<uint64> MySQLDatabase::GetAccountIdByName(const std::string& name)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		mysql::Select select(m_connection, "SELECT id FROM `account` WHERE username = '" + m_connection.EscapeString(name) + "' LIMIT 1");
		if (select.Success())
		{
			mysql::Row row(select);
			if (row)
			{
				return std::strtoull(row.GetField(0), nullptr, 10);
			}
		}
		else
		{
			PrintDatabaseError();
		}

		return {};
	}

	bool MySQLDatabase::GrantFeature(uint32 featureId, const std::vector<uint64>& accountIds, const std::string& expiration)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		if (accountIds.empty())
		{
			return true;
		}

		const std::string expirationValue = expiration.empty() ? "NULL" : "'" + m_connection.EscapeString(expiration) + "'";

		std::ostringstream query;
		query << "INSERT INTO `account_feature` (account_id, feature_id, expiration) VALUES ";
		for (size_t i = 0; i < accountIds.size(); ++i)
		{
			if (i > 0)
			{
				query << ", ";
			}
			query << "(" << accountIds[i] << ", " << featureId << ", " << expirationValue << ")";
		}
		query << " ON DUPLICATE KEY UPDATE expiration = VALUES(expiration), granted_at = NOW()";

		if (!m_connection.Execute(query.str()))
		{
			PrintDatabaseError();
			return false;
		}

		return true;
	}

	bool MySQLDatabase::RevokeFeature(uint32 featureId, const std::vector<uint64>& accountIds)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		if (accountIds.empty())
		{
			return true;
		}

		std::ostringstream query;
		query << "DELETE FROM `account_feature` WHERE feature_id = " << featureId << " AND account_id IN (";
		for (size_t i = 0; i < accountIds.size(); ++i)
		{
			if (i > 0)
			{
				query << ", ";
			}
			query << accountIds[i];
		}
		query << ")";

		if (!m_connection.Execute(query.str()))
		{
			PrintDatabaseError();
			return false;
		}

		return true;
	}

	std::vector<AccountFeature> MySQLDatabase::GetActiveAccountFeatures(uint64 accountId)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		std::vector<AccountFeature> features;

		mysql::Select select(m_connection,
			"SELECT f.id, f.name, IFNULL(DATE_FORMAT(af.expiration,'%Y-%m-%dT%H:%i:%sZ'),'') "
			"FROM `account_feature` af "
			"INNER JOIN `feature` f ON f.id = af.feature_id "
			"WHERE af.account_id = " + std::to_string(accountId) +
			" AND (af.expiration IS NULL OR af.expiration > NOW()) "
			"ORDER BY f.name");
		if (!select.Success())
		{
			PrintDatabaseError();
			return features;
		}

		mysql::Row row(select);
		while (row)
		{
			AccountFeature entry;
			entry.id = static_cast<uint32>(std::strtoul(row.GetField(0), nullptr, 10));
			entry.key = row.GetField(1) ? row.GetField(1) : "";
			entry.expiration = row.GetField(2) ? row.GetField(2) : "";
			features.push_back(std::move(entry));
			row = mysql::Row(select);
		}

		return features;
	}

	bool MySQLDatabase::SetRealmFeatureRequirement(uint32 realmId, uint32 featureId, bool requireVisibility, bool requireLogin)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		std::ostringstream query;
		query << "INSERT INTO `realm_feature_requirement` (realm_id, feature_id, require_visibility, require_login) VALUES ("
			<< realmId << ", " << featureId << ", " << (requireVisibility ? 1 : 0) << ", " << (requireLogin ? 1 : 0) << ") "
			"ON DUPLICATE KEY UPDATE require_visibility = VALUES(require_visibility), require_login = VALUES(require_login)";

		if (!m_connection.Execute(query.str()))
		{
			PrintDatabaseError();
			return false;
		}

		return true;
	}

	bool MySQLDatabase::RemoveRealmFeatureRequirement(uint32 realmId, uint32 featureId)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		if (!m_connection.Execute("DELETE FROM `realm_feature_requirement` WHERE realm_id = "
			+ std::to_string(realmId) + " AND feature_id = " + std::to_string(featureId) + " LIMIT 1"))
		{
			PrintDatabaseError();
			return false;
		}

		return mysql_affected_rows(m_connection.GetHandle()) > 0;
	}

	std::vector<RealmFeatureRequirement> MySQLDatabase::GetRealmFeatureRequirements(uint32 realmId)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		std::vector<RealmFeatureRequirement> requirements;

		mysql::Select select(m_connection,
			"SELECT rfr.feature_id, f.name, rfr.require_visibility, rfr.require_login "
			"FROM `realm_feature_requirement` rfr "
			"INNER JOIN `feature` f ON f.id = rfr.feature_id "
			"WHERE rfr.realm_id = " + std::to_string(realmId) +
			" ORDER BY f.name");
		if (!select.Success())
		{
			PrintDatabaseError();
			return requirements;
		}

		mysql::Row row(select);
		while (row)
		{
			RealmFeatureRequirement entry;
			entry.featureId = static_cast<uint32>(std::strtoul(row.GetField(0), nullptr, 10));
			entry.featureName = row.GetField(1) ? row.GetField(1) : "";
			entry.requireVisibility = std::atoi(row.GetField(2)) != 0;
			entry.requireLogin = std::atoi(row.GetField(3)) != 0;
			requirements.push_back(std::move(entry));
			row = mysql::Row(select);
		}

		return requirements;
	}

	std::optional<uint32> MySQLDatabase::GetRealmIdByName(const std::string& name)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		mysql::Select select(m_connection, "SELECT id FROM `realm` WHERE name = '" + m_connection.EscapeString(name) + "' LIMIT 1");
		if (select.Success())
		{
			mysql::Row row(select);
			if (row)
			{
				return static_cast<uint32>(std::strtoul(row.GetField(0), nullptr, 10));
			}
		}
		else
		{
			PrintDatabaseError();
		}

		return {};
	}

	std::optional<AccountAuthData> MySQLDatabase::GetAccountAuthData(std::string accountName)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		mysql::Select select(m_connection, "SELECT id, k, gm_level FROM account WHERE username = '" + m_connection.EscapeString(accountName) + "' LIMIT 1");
		if (!select.Success())
		{
			PrintDatabaseError();
			return {};
		}

		mysql::Row row(select);
		if (!row)
		{
			return {};
		}

		AccountAuthData data;
		data.id = std::strtoull(row.GetField(0), nullptr, 10);
		data.sessionKey = row.GetField(1) ? row.GetField(1) : "";
		data.gmLevel = static_cast<uint8>(std::atoi(row.GetField(2)));
		data.features = GetActiveAccountFeatures(data.id);
		return data;
	}

	namespace
	{
		/// SQL comparison suffix limiting a datetime column to the given range window.
		/// Appended after the column name, e.g. "`timestamp`" + StatsRangeWindow(range).
		const char* StatsRangeWindow(StatsRange range)
		{
			switch (range)
			{
			case StatsRange::Day:   return " >= NOW() - INTERVAL 24 HOUR";
			case StatsRange::Week:  return " >= NOW() - INTERVAL 7 DAY";
			case StatsRange::Month: return " >= NOW() - INTERVAL 30 DAY";
			}
			return " >= NOW() - INTERVAL 7 DAY";
		}

		/// MySQL DATE_FORMAT pattern used to bucket a range: hourly for a day, daily otherwise.
		const char* StatsRangeBucketFormat(StatsRange range)
		{
			return range == StatsRange::Day ? "%Y-%m-%d %H:00" : "%Y-%m-%d";
		}

		/// Reads a two-column (key, value) result set into a list of StatsBucket entries.
		std::vector<StatsBucket> ReadBuckets(mysql::Select& select)
		{
			std::vector<StatsBucket> buckets;
			mysql::Row row(select);
			while (row)
			{
				StatsBucket bucket;
				bucket.key = row.GetField(0) ? row.GetField(0) : "";
				bucket.value = row.GetField(1) ? std::strtoull(row.GetField(1), nullptr, 10) : 0;
				buckets.push_back(std::move(bucket));
				row = mysql::Row(select);
			}
			return buckets;
		}
	}

	StatsSummary MySQLDatabase::GetStatsSummary()
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		StatsSummary summary;

		mysql::Select select(m_connection,
			"SELECT "
			"(SELECT COUNT(*) FROM `account`), "
			"(SELECT COUNT(*) FROM `account` WHERE banned <> 0 AND (ban_expiration IS NULL OR ban_expiration > NOW()))");
		if (!select.Success())
		{
			PrintDatabaseError();
			return summary;
		}

		mysql::Row row(select);
		if (row)
		{
			summary.totalAccounts = row.GetField(0) ? std::strtoull(row.GetField(0), nullptr, 10) : 0;
			summary.bannedAccounts = row.GetField(1) ? std::strtoull(row.GetField(1), nullptr, 10) : 0;
		}

		return summary;
	}

	std::vector<StatsBucket> MySQLDatabase::GetLoginTimeSeries(StatsRange range)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		const std::string sql =
			std::string("SELECT DATE_FORMAT(`timestamp`,'") + StatsRangeBucketFormat(range) + "') AS bucket, COUNT(*) "
			"FROM `account_login` "
			"WHERE succeeded = 1 AND `timestamp`" + StatsRangeWindow(range) + " "
			"GROUP BY bucket ORDER BY bucket";

		mysql::Select select(m_connection, sql);
		if (!select.Success())
		{
			PrintDatabaseError();
			return {};
		}

		return ReadBuckets(select);
	}

	std::vector<StatsBucket> MySQLDatabase::GetRegistrationTimeSeries(StatsRange range)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		const std::string sql =
			std::string("SELECT DATE_FORMAT(`created_at`,'") + StatsRangeBucketFormat(range) + "') AS bucket, COUNT(*) "
			"FROM `account` "
			"WHERE `created_at`" + StatsRangeWindow(range) + " "
			"GROUP BY bucket ORDER BY bucket";

		mysql::Select select(m_connection, sql);
		if (!select.Success())
		{
			PrintDatabaseError();
			return {};
		}

		return ReadBuckets(select);
	}

	std::vector<StatsBucket> MySQLDatabase::GetPlayerCountTimeSeries(StatsRange range, std::optional<uint32> realmId)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		// Each sampling pass writes one row per realm sharing the same NOW() timestamp. For a global
		// series we first sum the realms per sample instant, then take the peak (max) per bucket so
		// short-lived peaks between samples are preserved rather than averaged away. For a single
		// realm we skip the summation step.
		const std::string bucketFmt = StatsRangeBucketFormat(range);
		const std::string window = StatsRangeWindow(range);

		std::string sql;
		if (realmId)
		{
			sql =
				"SELECT DATE_FORMAT(`timestamp`,'" + bucketFmt + "') AS bucket, MAX(player_count) "
				"FROM `player_count_sample` "
				"WHERE `timestamp`" + window + " AND realm_id = " + std::to_string(*realmId) + " "
				"GROUP BY bucket ORDER BY bucket";
		}
		else
		{
			sql =
				"SELECT DATE_FORMAT(`timestamp`,'" + bucketFmt + "') AS bucket, MAX(total) FROM ("
				"  SELECT `timestamp`, SUM(player_count) AS total "
				"  FROM `player_count_sample` "
				"  WHERE `timestamp`" + window + " "
				"  GROUP BY `timestamp`"
				") AS per_instant "
				"GROUP BY DATE_FORMAT(`timestamp`,'" + bucketFmt + "') ORDER BY bucket";
		}

		mysql::Select select(m_connection, sql);
		if (!select.Success())
		{
			PrintDatabaseError();
			return {};
		}

		return ReadBuckets(select);
	}

	std::vector<RecentActivityEntry> MySQLDatabase::GetRecentActivity(uint32 limit)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		std::vector<RecentActivityEntry> entries;

		const std::string sql =
			"SELECT type, detail, DATE_FORMAT(ts,'%Y-%m-%dT%H:%i:%sZ') FROM ("
			"  SELECT 'login' AS type, a.username AS detail, al.`timestamp` AS ts "
			"  FROM `account_login` al JOIN `account` a ON a.id = al.account_id "
			"  WHERE al.succeeded = 1 "
			"  UNION ALL "
			"  SELECT 'register' AS type, username AS detail, created_at AS ts "
			"  FROM `account` "
			") AS feed ORDER BY ts DESC LIMIT " + std::to_string(limit);

		mysql::Select select(m_connection, sql);
		if (!select.Success())
		{
			PrintDatabaseError();
			return entries;
		}

		mysql::Row row(select);
		while (row)
		{
			RecentActivityEntry entry;
			entry.type = row.GetField(0) ? row.GetField(0) : "";
			entry.detail = row.GetField(1) ? row.GetField(1) : "";
			entry.timestamp = row.GetField(2) ? row.GetField(2) : "";
			entries.push_back(std::move(entry));
			row = mysql::Row(select);
		}

		return entries;
	}

	void MySQLDatabase::AddPlayerCountSample(uint32 realmId, uint32 playerCount)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		if (!m_connection.Execute("INSERT INTO `player_count_sample` (realm_id, `timestamp`, player_count) VALUES ("
			+ std::to_string(realmId) + ", NOW(), " + std::to_string(playerCount) + ")"))
		{
			PrintDatabaseError();
		}
	}

	void MySQLDatabase::PrintDatabaseError()
	{
		ELOG("Login database error: " << m_connection.GetErrorMessage());
	}
}
