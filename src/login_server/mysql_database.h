// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "database.h"
#include "base/countdown.h"
#include "mysql_wrapper/mysql_connection.h"

namespace mmo
{
	/// MySQL implementation of the login server database system.
	class MySQLDatabase final 
		: public IDatabase
	{
	public:
		explicit MySQLDatabase(mysql::DatabaseInfo connectionInfo, TimerQueue& timerQueue);
		~MySQLDatabase() override = default;

		/// Tries to establish a connection to the MySQL server.
		bool Load();

	private:
		void SetNextPingTimer() const;

	public:
		std::optional<AccountData> GetAccountDataByName(std::string name) override;

		std::optional<RealmAuthData> GetRealmAuthData(std::string name) override;

		/// Retrieves the session key, account id, and GM level by name.
		/// @param accountName Name of the account.
		std::optional<std::tuple<uint64, std::string, uint8>> GetAccountSessionKey(std::string accountName) override;

		/// Sets the GM level for an account.
		/// @param accountName Name of the account.
		/// @param gmLevel New GM level to set.
		bool SetAccountGMLevel(std::string accountName, uint8 gmLevel) override;

		void PlayerLogin(uint64 accountId, const std::string& sessionKey, const std::string& ip) override;

		void PlayerLoginFailed(uint64 accountId, const std::string& ip) override;

		void RealmLogin(uint32 realmId, const std::string& sessionKey, const std::string& ip, const std::string& build) override;

		std::optional<AccountCreationResult> AccountCreate(const std::string& id, const std::string& s, const std::string& v) override;

		std::optional<RealmCreationResult> RealmCreate(const std::string& name, const std::string& address, uint16 port, const std::string& s, const std::string& v) override;

		void BanAccountByName(const std::string& accountName, const std::string& expiration, const std::string& reason) override;

		void UnbanAccountByName(const std::string& accountName, const std::string& reason) override;

	private:
		void PrintDatabaseError();

	private:
		mysql::DatabaseInfo m_connectionInfo;
		mysql::Connection m_connection;
		TimerQueue& m_timerQueue;
		Countdown m_pingCountdown;
		scoped_connection m_pingConnection;
	};
}
