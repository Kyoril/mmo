// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "log/log_exception.h"
#include "base/sha1.h"
#include "game/character_view.h"

#include <functional>
#include <exception>
#include <optional>
#include <vector>


namespace mmo
{
	class Degree;
	class Vector3;

	/// Contains data used by a world for authentication.
	struct WorldAuthData final
	{
		/// The unique world id.
		uint64 id;
		/// Name of the world.
		std::string name;
		/// Password salt.
		std::string s;
		/// Password verifier.
		std::string v;
	};
	
	/// Basic interface for a database system used by the login server.
	struct IDatabase : public NonCopyable
	{
		virtual ~IDatabase();

		/// Gets the list of characters that belong to a certain character id.
		/// @param accountId Id of the account.
		virtual std::optional<std::vector<CharacterView>> GetCharacterViewsByAccountId(uint64 accountId) = 0;
		/// Obtains world data by it's name.
		/// @param name Name of the world.
		virtual std::optional<WorldAuthData> GetWorldAuthData(std::string name) = 0;
		/// 
		virtual void WorldLogin(uint64 worldId, const std::string& sessionKey, const std::string& ip, const std::string& build) = 0;
		/// Deletes a character with the given guid.
		virtual void DeleteCharacter(uint64 characterGuid) = 0;
		/// Creates a new character on the given account.
		virtual void CreateCharacter(std::string characterName, uint64 accountId, uint32 map, uint32 level, uint32 hp, uint32 gender, uint32 race, const Vector3& position, const Degree& orientation) = 0;
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
