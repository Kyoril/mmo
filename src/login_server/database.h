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
