// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "base/async_database.h"
#include "log/log_exception.h"
#include <functional>
#include <exception>
#include <optional>

#include "base/sha1.h"

namespace mmo
{
	enum class AccountCreationResult : uint8
	{
		Success,

		AccountNameAlreadyInUse,
		InternalServerError
	};

	enum class RealmCreationResult : uint8
	{
		Success,

		RealmNameAlreadyInUse,
		InternalServerError
	};

	enum class BanState : uint8
	{
		None,

		Temporarily,
		Permanent
	};

	/// Contains account data in a structure.
	struct AccountData
	{
		/// The unique account id.
		uint64 id;
		/// The account name.
		std::string name;
		/// The account password salt.
		std::string s;
		/// The account password verifier.
		std::string v;

		BanState banned;
	};

	/// Contains data used by a realm for authentification.
	struct RealmAuthData
	{
		/// The unique realm id.
		uint32 id;
		/// Name of the realm.
		std::string name;
		/// Password salt.
		std::string s;
		/// Password verifier.
		std::string v;
		/// The ip address of the realm server.
		std::string ipAddress;
		/// The port of the realm server.
		uint16 port;
	};

	/// Parameters for paginated account list queries.
	struct AccountListParams
	{
		std::string search;
		std::string searchField;   // "name", "email", "id"
		std::string sortBy;        // "id", "username", "last_login", "created_at"
		bool sortAsc = true;
		bool bannedOnly = false;
		uint32 page = 1;
		uint32 limit = 100;
	};

	/// Single row returned by GetAccountList.
	struct AccountListEntry
	{
		uint64 id = 0;
		std::string username;
		std::string email;
		std::string created_at;
		std::string last_login;
		std::string last_ip;
		uint8 gm_level = 0;
		uint8 ban_state = 0;       // 0=none, 1=temp, 2=perm
		std::string ban_expiration;
	};

	struct AccountListResult
	{
		std::vector<AccountListEntry> accounts;
		uint64 total = 0;
	};

	/// A feature definition from the catalog.
	struct FeatureDefinition
	{
		uint32 id = 0;
		std::string name;
		std::string description;
		std::string created_at;
	};

	/// An active feature granted to an account (id + key).
	struct AccountFeature
	{
		uint32 id = 0;
		std::string key;
		std::string expiration;   // empty means it never expires
	};

	/// A single feature requirement declared by a realm.
	struct RealmFeatureRequirement
	{
		uint32 featureId = 0;
		std::string featureName;
		bool requireVisibility = false;
		bool requireLogin = false;
	};

	/// Time range selector for dashboard statistics queries.
	enum class StatsRange : uint8
	{
		/// Last 24 hours, bucketed per hour.
		Day,
		/// Last 7 days, bucketed per day.
		Week,
		/// Last 30 days, bucketed per day.
		Month
	};

	/// A single (sparse) bucket of a statistics time series. Buckets with no data are simply
	/// not returned; the consumer is responsible for zero-filling the gaps.
	struct StatsBucket
	{
		/// Bucket key as formatted by the database, e.g. "2026-06-24 14:00" (hourly) or
		/// "2026-06-24" (daily). Always uses the database server's local time.
		std::string key;
		/// Aggregated value for the bucket (login/registration count, or peak player count).
		uint64 value = 0;
	};

	/// Headline counters for the dashboard info boxes that can be answered from the login database.
	struct StatsSummary
	{
		/// Total number of registered game accounts.
		uint64 totalAccounts = 0;
		/// Number of accounts with an active (non-expired) ban.
		uint64 bannedAccounts = 0;
	};

	/// A single recent-activity feed entry.
	struct RecentActivityEntry
	{
		/// Event type, e.g. "login" or "register".
		std::string type;
		/// Human readable detail, typically the account name.
		std::string detail;
		/// Event timestamp formatted as ISO8601.
		std::string timestamp;
	};

	/// All data needed to validate and finish a client auth session on a realm.
	struct AccountAuthData
	{
		uint64 id = 0;
		std::string sessionKey;
		uint8 gmLevel = 0;
		std::vector<AccountFeature> features;
	};

	/// Single row returned by GetRealmList.
	struct RealmListEntry
	{
		uint32 id = 0;
		std::string name;
		std::string address;
		uint16 port = 0;
		std::string last_login;
		std::string last_ip;
		std::string last_build;
		bool is_online = false;
	};

	/// Basic interface for a database system used by the login server.
	struct IDatabase : public NonCopyable
	{
		virtual ~IDatabase();

		/// Gets the account data by a given name.
		/// @param name Name of the account.
		virtual std::optional<AccountData> GetAccountDataByName(std::string name) = 0;

		/// Obtains realm data by it's id.
		/// @param name Name of the realm.
		virtual std::optional<RealmAuthData> GetRealmAuthData(std::string name) = 0;

		/// Retrieves the session key, account id, and GM level by name.
		/// @param accountName Name of the account.
		virtual std::optional<std::tuple<uint64, std::string, uint8>> GetAccountSessionKey(std::string accountName) = 0;

		/// Sets the GM level for an account.
		/// @param accountName Name of the account.
		/// @param gmLevel New GM level to set.
		virtual bool SetAccountGMLevel(std::string accountName, uint8 gmLevel) = 0;

		/// Writes player session and login data to the database. This also writes the current timestamp
		/// to the last_login field.
		/// @param accountId ID of the account to modify.
		/// @param sessionKey The new session key (hex str).
		/// @param ip The current ip of the player.
		virtual void PlayerLogin(uint64 accountId, const std::string& sessionKey, const std::string& ip) = 0;

		/// Records a failed login attempt for the account.
		/// @param accountId ID of the account that failed to log in.
		/// @param ip The current ip of the player.
		virtual void PlayerLoginFailed(uint64 accountId, const std::string& ip) = 0;

		/// Writes realm session data to the database after a successful login.
		/// @param realmId ID of the realm to modify.
		/// @param sessionKey The new session key (hex str).
		/// @param ip The current ip of the realm.
		/// @param build The realm build string.
		virtual void RealmLogin(uint32 realmId, const std::string& sessionKey, const std::string& ip, const std::string& build) = 0;

		/// Creates a new account with the given salted password verifier.
		/// @param id Account name (typically normalized to uppercase).
		/// @param s SRP salt value.
		/// @param v SRP verifier value.
		virtual std::optional<AccountCreationResult> AccountCreate(const std::string& id, const std::string& s, const std::string& v) = 0;

		/// Creates a new realm with the given connection info and SRP values.
		/// @param name Realm name.
		/// @param address Realm public address.
		/// @param port Realm public port.
		/// @param s SRP salt value.
		/// @param v SRP verifier value.
		virtual std::optional<RealmCreationResult> RealmCreate(const std::string& name, const std::string& address, uint16 port, const std::string& s, const std::string& v) = 0;

		/// Bans an account by name.
		/// @param accountName Account name to ban.
		/// @param expiration Expiration timestamp string.
		/// @param reason Reason for the ban.
		virtual void BanAccountByName(const std::string& accountName, const std::string& expiration, const std::string& reason) = 0;

		/// Removes a ban from an account by name.
		/// @param accountName Account name to unban.
		/// @param reason Reason for the unban.
		virtual void UnbanAccountByName(const std::string& accountName, const std::string& reason) = 0;

		/// Returns a paginated, filtered, sorted list of accounts.
		virtual AccountListResult GetAccountList(const AccountListParams& params) = 0;

		/// Returns every registered realm row from the database.
		virtual std::vector<RealmListEntry> GetRealmList() = 0;

		/// Returns the whole feature catalog.
		virtual std::vector<FeatureDefinition> GetFeatures() = 0;

		/// Creates a new feature in the catalog.
		/// @param name Unique feature key.
		/// @param description Optional human readable description.
		/// @returns The new feature id on success, an empty optional if the name is already in use.
		virtual std::optional<uint32> CreateFeature(const std::string& name, const std::string& description) = 0;

		/// Deletes a feature from the catalog by its id (also removes account grants and realm requirements via cascade).
		/// @returns true if a feature was deleted.
		virtual bool DeleteFeature(uint32 featureId) = 0;

		/// Resolves a feature by name. @returns The feature id or empty optional if not found.
		virtual std::optional<uint32> GetFeatureIdByName(const std::string& name) = 0;

		/// Resolves an account id by name. @returns The account id or empty optional if not found.
		virtual std::optional<uint64> GetAccountIdByName(const std::string& name) = 0;

		/// Grants a feature to one or more accounts.
		/// @param featureId The feature to grant.
		/// @param accountIds The accounts that should receive the feature.
		/// @param expiration Optional expiration timestamp (empty means permanent).
		virtual bool GrantFeature(uint32 featureId, const std::vector<uint64>& accountIds, const std::string& expiration) = 0;

		/// Revokes a feature from one or more accounts.
		virtual bool RevokeFeature(uint32 featureId, const std::vector<uint64>& accountIds) = 0;

		/// Returns the active (non-expired) features granted to an account.
		virtual std::vector<AccountFeature> GetActiveAccountFeatures(uint64 accountId) = 0;

		/// Sets (inserts or updates) a realm feature requirement.
		virtual bool SetRealmFeatureRequirement(uint32 realmId, uint32 featureId, bool requireVisibility, bool requireLogin) = 0;

		/// Removes a realm feature requirement.
		virtual bool RemoveRealmFeatureRequirement(uint32 realmId, uint32 featureId) = 0;

		/// Returns all feature requirements declared by a realm.
		virtual std::vector<RealmFeatureRequirement> GetRealmFeatureRequirements(uint32 realmId) = 0;

		/// Returns the realm id by name. @returns The realm id or empty optional if not found.
		virtual std::optional<uint32> GetRealmIdByName(const std::string& name) = 0;

		/// Retrieves all data needed to validate and finish a client auth session, including the
		/// account's active feature keys.
		/// @param accountName Name of the account.
		virtual std::optional<AccountAuthData> GetAccountAuthData(std::string accountName) = 0;

		/// Returns headline counters (account and active-ban totals) for the admin dashboard.
		virtual StatsSummary GetStatsSummary() = 0;

		/// Returns the number of successful logins over time, bucketed per the given range.
		virtual std::vector<StatsBucket> GetLoginTimeSeries(StatsRange range) = 0;

		/// Returns the number of new account registrations over time, bucketed per the given range.
		virtual std::vector<StatsBucket> GetRegistrationTimeSeries(StatsRange range) = 0;

		/// Returns the peak concurrent player count over time, bucketed per the given range.
		/// @param realmId If set, only samples for that realm are considered; otherwise the totals
		///        across all realms (summed per sample instant) are used.
		virtual std::vector<StatsBucket> GetPlayerCountTimeSeries(StatsRange range, std::optional<uint32> realmId) = 0;

		/// Returns the most recent login and registration events for the activity feed.
		/// @param limit Maximum number of entries to return.
		virtual std::vector<RecentActivityEntry> GetRecentActivity(uint32 limit) = 0;

		/// Appends a concurrent-player-count sample for a realm at the current time.
		/// @param realmId The realm the sample belongs to.
		/// @param playerCount The number of players connected to that realm.
		virtual void AddPlayerCountSample(uint32 realmId, uint32 playerCount) = 0;
	};

	/// Async database wrapper for the login server.
	/// Inherits from the shared AsyncDatabaseT template, bound to this server's IDatabase interface.
	class AsyncDatabase final : public AsyncDatabaseT<IDatabase>
	{
	public:
		using AsyncDatabaseT<IDatabase>::AsyncDatabaseT;
	};

	typedef std::function<void()> Action;
}
