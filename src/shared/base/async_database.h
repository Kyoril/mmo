// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "non_copyable.h"
#include "log/log_exception.h"

#include <functional>
#include <exception>

namespace mmo
{
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

	/// Helper class for async database operations.
	/// @tparam TDatabase The database interface type. Member-function pointers passed to
	///         asyncRequest must be members of this type.
	template <class TDatabase>
	class AsyncDatabaseT : public NonCopyable
	{
	public:
		typedef std::function<void(const std::function<void()> &)> ActionDispatcher;

		/// Initializes this class by assigning a database and worker callbacks.
		///
		/// @param database        The database instance to invoke requests on.
		/// @param asyncWorker     Queues work onto the database worker thread.
		/// @param resultDispatcher Queues result callbacks back onto the main/IO thread.
		explicit AsyncDatabaseT(TDatabase &database,
			ActionDispatcher asyncWorker,
			ActionDispatcher resultDispatcher)
			: m_database(database)
			, m_asyncWorker(std::move(asyncWorker))
			, m_resultDispatcher(std::move(resultDispatcher))
		{
		}

	public:
		/// Fire-and-forget: calls a void member function with one argument on the DB thread.
		template <class A0, class B0_>
		void asyncRequest(void(TDatabase::*method)(A0), B0_ &&b0)
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

		/// Calls a returning member function with arguments on the DB thread; invokes handler on the main thread.
		template <class ResultHandler, class Result, class... A0, class... Args>
		void asyncRequest(ResultHandler &&handler, Result(TDatabase::*method)(A0...), Args&&... b0)
		{
			auto request = std::bind(method, &m_database, std::forward<Args>(b0)...);
			auto processor = [this, request, handler]() -> void
			{
				detail::RequestProcessor<Result> proc;
				proc(m_resultDispatcher, request, handler);
			};
			m_asyncWorker(processor);
		}

		/// Calls an arbitrary callable (lambda / bound function) on the DB thread; invokes handler on the main thread.
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
		TDatabase &m_database;
		const ActionDispatcher m_asyncWorker;
		const ActionDispatcher m_resultDispatcher;
	};

}
