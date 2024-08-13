// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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

#include "game/character_data.h"


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

	enum class WorldCreationResult : uint8
	{
		Success,

		WorldNameAlreadyInUse,
		InternalServerError
	};

	enum class CharCreateResult : uint8
	{
		Success,

		NameAlreadyInUse,
		Error
	};

	/// Basic interface for a database system used by the login server.
	struct IDatabase : public NonCopyable
	{
		virtual ~IDatabase() override;

		/// Gets the list of characters that belong to a certain character id.
		/// @param accountId Id of the account.
		virtual std::optional<std::vector<CharacterView>> GetCharacterViewsByAccountId(uint64 accountId) = 0;

		/// Obtains world data by it's name.
		/// @param name Name of the world.
		virtual std::optional<WorldAuthData> GetWorldAuthData(std::string name) = 0;

		/// Handles a successful login request for a world by storing it's information into the database.
		///	@param worldId Id of the world.
		///	@param sessionKey The current world connection session key.
		///	@param ip The current public ip address of the world.
		///	@param build The build version of the world node.
		virtual void WorldLogin(uint64 worldId, const std::string& sessionKey, const std::string& ip, const std::string& build) = 0;

		/// Deletes a character with the given guid.
		///	@param characterGuid Unique id of the character to delete.
		virtual void DeleteCharacter(uint64 characterGuid) = 0;

		/// Creates a new character on the given account.
		///	@param characterName Name of the character. Has to be unique on the realm.
		///	@param accountId Id of the account the character belongs to.
		///	@param map The map id where the character is spawned.
		///	@param level Start level of the character.
		///	@param hp Current amount of health of the character.
		///	@param gender The characters gender.
		///	@param race The character race.
		///	@param position Position of the character on the map.
		///	@param orientation Facing of the character in the world.
		virtual std::optional<CharCreateResult> CreateCharacter(std::string characterName, uint64 accountId, uint32 map, uint32 level, uint32 hp, uint32 gender, uint32 race, uint32 characterClass, const Vector3& position, const Degree& orientation, std::vector<uint32> spellIds) = 0;

		/// Loads character data of a character who wants to enter a world.
		///	@param characterId Unique id of the character to load.
		///	@param accountId Id of the player account to prevent players from logging in with another account's character.
		///	@returns Character data of the character, if the character exists.
		virtual std::optional<CharacterData> CharacterEnterWorld(uint64 characterId, uint64 accountId) = 0;
		
		virtual std::optional<WorldCreationResult> CreateWorkd(const String& name, const String& s, const String& v) = 0;

		virtual void ChatMessage(uint64 characterId, uint16 type, String message) = 0;
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
		template <class ResultHandler, class Result, class... A0, class... Args>
		void asyncRequest(ResultHandler &&handler, Result(IDatabase::*method)(A0...), Args&&... b0)
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
