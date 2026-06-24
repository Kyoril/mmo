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
		/// Creates a MySQL database instance.
		explicit MySQLDatabase(mysql::DatabaseInfo connectionInfo, TimerQueue& timerQueue);
		~MySQLDatabase() override = default;

		/// Tries to establish a connection to the MySQL server.
		bool Load();

	private:
		/// Schedules the next keep-alive ping to the database.
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

		/// Writes player session and login data to the database.
		void PlayerLogin(uint64 accountId, const std::string& sessionKey, const std::string& ip) override;

		/// Records a failed login attempt for a player account.
		void PlayerLoginFailed(uint64 accountId, const std::string& ip) override;

		/// Writes realm session data to the database after a successful login.
		void RealmLogin(uint32 realmId, const std::string& sessionKey, const std::string& ip, const std::string& build) override;

		/// Creates a new account with the given SRP values.
		std::optional<AccountCreationResult> AccountCreate(const std::string& id, const std::string& s, const std::string& v) override;

		/// Creates a new realm with the given SRP values.
		std::optional<RealmCreationResult> RealmCreate(const std::string& name, const std::string& address, uint16 port, const std::string& s, const std::string& v) override;

		/// Bans an account by name.
		void BanAccountByName(const std::string& accountName, const std::string& expiration, const std::string& reason) override;

		/// Removes a ban from an account by name.
		void UnbanAccountByName(const std::string& accountName, const std::string& reason) override;

		/// Returns a paginated, filtered, sorted list of accounts.
		AccountListResult GetAccountList(const AccountListParams& params) override;

		/// Returns every registered realm row from the database.
		std::vector<RealmListEntry> GetRealmList() override;

		std::vector<FeatureDefinition> GetFeatures() override;
		std::optional<uint32> CreateFeature(const std::string& name, const std::string& description) override;
		bool DeleteFeature(uint32 featureId) override;
		std::optional<uint32> GetFeatureIdByName(const std::string& name) override;
		std::optional<uint64> GetAccountIdByName(const std::string& name) override;
		bool GrantFeature(uint32 featureId, const std::vector<uint64>& accountIds, const std::string& expiration) override;
		bool RevokeFeature(uint32 featureId, const std::vector<uint64>& accountIds) override;
		std::vector<AccountFeature> GetActiveAccountFeatures(uint64 accountId) override;
		bool SetRealmFeatureRequirement(uint32 realmId, uint32 featureId, bool requireVisibility, bool requireLogin) override;
		bool RemoveRealmFeatureRequirement(uint32 realmId, uint32 featureId) override;
		std::vector<RealmFeatureRequirement> GetRealmFeatureRequirements(uint32 realmId) override;
		std::optional<uint32> GetRealmIdByName(const std::string& name) override;
		std::optional<AccountAuthData> GetAccountAuthData(std::string accountName) override;
		StatsSummary GetStatsSummary() override;
		std::vector<StatsBucket> GetLoginTimeSeries(StatsRange range) override;
		std::vector<StatsBucket> GetRegistrationTimeSeries(StatsRange range) override;
		std::vector<StatsBucket> GetPlayerCountTimeSeries(StatsRange range, std::optional<uint32> realmId) override;
		std::vector<RecentActivityEntry> GetRecentActivity(uint32 limit) override;
		void AddPlayerCountSample(uint32 realmId, uint32 playerCount) override;

	private:
		/// Logs the last database error to the default logger.
		void PrintDatabaseError();

	private:
		mysql::DatabaseInfo m_connectionInfo;
		mysql::Connection m_connection;
		TimerQueue& m_timerQueue;
		Countdown m_pingCountdown;
		scoped_connection m_pingConnection;
	};
}
