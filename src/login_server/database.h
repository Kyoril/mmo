// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
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
		virtual std::optional<AccountData> getAccountDataByName(std::string name) = 0;

		/// Obtains realm data by it's id.
		/// @param name Name of the realm.
		virtual std::optional<RealmAuthData> getRealmAuthData(std::string name) = 0;

		/// Retrieves the session key and the account id by name.
		/// @param accountName Name of the account.
		virtual std::optional<std::pair<uint64, std::string>> getAccountSessionKey(std::string accountName) = 0;

		/// Writes player session and login data to the database. This also writes the current timestamp
		/// to the last_login field.
		/// @param accountId ID of the account to modify.
		/// @param sessionKey The new session key (hex str).
		/// @param ip The current ip of the player.
		virtual void playerLogin(uint64 accountId, const std::string& sessionKey, const std::string& ip) = 0;

		/// @brief 
		/// @param accountId 
		/// @param ip 
		virtual void playerLoginFailed(uint64 accountId, const std::string& ip) = 0;

		/// @brief 
		/// @param realmId 
		/// @param sessionKey 
		/// @param ip 
		/// @param build 
		virtual void realmLogin(uint32 realmId, const std::string& sessionKey, const std::string& ip, const std::string& build) = 0;

		virtual std::optional<AccountCreationResult> accountCreate(const std::string& id, const std::string& s, const std::string& v) = 0;

		virtual std::optional<RealmCreationResult> realmCreate(const std::string& name, const std::string& address, uint16 port, const std::string& s, const std::string& v) = 0;
	};


	namespace detail
	{
		template <class Result>
		struct RequestProcessor
		{
			template <class ResultDispatcher, class Request, class ResultHandler>
			void operator ()(const ResultDispatcher &dispatcher,
				const Request &request,
				const ResultHandler &handler) const
			{
				Result result;

				try
				{
					result = request();
				}
				catch (const std::exception &ex)
				{
					defaultLogException(ex);
					return;
				}

				dispatcher(std::bind<void>(handler, std::move(result)));
			}
		};

		template <>
		struct RequestProcessor<void>
		{
			template <class ResultDispatcher, class Request, class ResultHandler>
			void operator ()(const ResultDispatcher &dispatcher,
				const Request &request,
				const ResultHandler &handler) const
			{
				bool succeeded = false;
				try
				{
					request();
					succeeded = true;
				}
				catch (const std::exception &ex)
				{
					defaultLogException(ex);
					return;
				}

				dispatcher(std::bind<void>(handler, succeeded));
			}
		};
	}

	struct NullHandler
	{
		virtual void operator()()
		{
		}
	};

	static constexpr NullHandler dbNullHandler;

	/// Helper class for async database operations
	class AsyncDatabase final : public NonCopyable
	{
	public:
		typedef std::function<void(const std::function<void()> &)> ActionDispatcher;

		/// Initializes this class by assigning a database and worker callbacks.
		/// 
		/// @param database The linked database which will be passed in to database operations.
		/// @param asyncWorker Callback which should queue a request to the async worker queue.
		/// @param resultDispatcher Callback which should queue a result callback to the main worker queue.
		explicit AsyncDatabase(IDatabase &database,
			ActionDispatcher asyncWorker,
			ActionDispatcher resultDispatcher);

	public:
		/// Performs an async database request and allows passing exactly one argument to the database request.
		/// 
		/// @param method A request callback which will be executed on the database thread without blocking the caller.
		/// @param b0 Argument which will be forwarded to the handler.
		template <class A0, class B0_>
		void asyncRequest(void(IDatabase::*method)(A0), B0_ &&b0)
		{
			auto request = std::bind(method, &m_database, std::forward<B0_>(b0));
			auto processor = [request]() -> void {
				try
				{
					request();
				}
				catch (const std::exception& ex)
				{
					defaultLogException(ex);
				}
			};
			m_asyncWorker(processor);
		}

		/// Performs an async database request and allows passing exactly one argument to the database request.
		/// 
		/// @param handler A handler callback which will be executed after the request was successful.
		/// @param method A request callback which will be executed on the database thread without blocking the caller.
		/// @param b0 Argument which will be forwarded to the handler.
		template <class ResultHandler, class Result, class A0, class... Args>
		void asyncRequest(ResultHandler &&handler, Result(IDatabase::*method)(A0), Args&&... b0)
		{
			auto request = std::bind(method, &m_database, std::forward<Args>(b0)...);
			auto processor = [this, request, handler]() -> void
			{
				detail::RequestProcessor<Result> proc;
				proc(m_resultDispatcher, request, handler);
			};
			m_asyncWorker(processor);
		}

		/// Performs an async database request.
		/// 
		/// @param request A request callback which will be executed on the database thread without blocking the caller.
		/// @param handler A handler callback which will be executed after the request was successful.
		template <class Result, class ResultHandler, class RequestFunction>
		void asyncRequest(RequestFunction &&request, ResultHandler &&handler)
		{
			auto processor = [this, request, handler]() -> void
			{
				detail::RequestProcessor<Result> proc;
				auto boundRequest = std::bind(request, &m_database);
				proc(m_resultDispatcher, boundRequest, handler);
			};
			m_asyncWorker(std::move(processor));
		}

	private:
		/// The database instance to perform 
		IDatabase &m_database;
		/// Callback which will queue a request to the async worker queue.
		const ActionDispatcher m_asyncWorker;
		/// Callback which will queue a result callback to the main worker queue.
		const ActionDispatcher m_resultDispatcher;
	};

	typedef std::function<void()> Action;
}
