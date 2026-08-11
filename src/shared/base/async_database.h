// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "log/log_exception.h"
#include "base/database_pool.h"
#include "base/typedefs.h"

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
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
	///
	/// @tparam TDatabase The database interface type. Member-function pointers passed to
	///         asyncRequest must be members of this type or one of its bases.
	///
	/// Requests are handed to a dispatcher along with an ordering key rather than being bound to
	/// one database instance, because the instance is chosen by the pool from that key. See
	/// DatabasePool for why ordering is keyed rather than free.
	template <class TDatabase>
	class AsyncDatabaseT
	{
	public:
		/// Queues work onto the connection that `key` maps to.
		using WorkDispatcher = std::function<void(uint64 key, std::function<void(TDatabase&)>)>;

		/// Queues a result callback back onto the io thread.
		using ResultDispatcher = std::function<void(std::function<void()>)>;

		/// @param asyncWorker      Queues work onto a database connection, chosen by key.
		/// @param resultDispatcher Queues result callbacks back onto the main/IO thread.
		explicit AsyncDatabaseT(WorkDispatcher asyncWorker, ResultDispatcher resultDispatcher)
			: m_asyncWorker(std::move(asyncWorker))
			, m_resultDispatcher(std::move(resultDispatcher))
		{
		}

	public:
		/// Fire-and-forget call with one argument, on the global ordering key.
		///
		/// Unkeyed, so it runs on slot 0 -- exactly where every request went when there was one
		/// connection. Prefer asyncRequestKeyed for anything scoped to a character or account.
		template <class TBase, class A0, class B0_>
		void asyncRequest(void(TBase::*method)(A0), B0_ &&b0)
		{
			asyncRequestKeyed(database_key::Global, method, std::forward<B0_>(b0));
		}

		/// Fire-and-forget call with one argument, ordered against `key`.
		template <class TBase, class A0, class B0_>
		void asyncRequestKeyed(uint64 key, void(TBase::*method)(A0), B0_ &&b0)
		{
			// Direct-initialised into a tuple rather than captured by value. Some argument types
			// here -- AvatarConfiguration, for one -- declare an *explicit* copy constructor, and
			// a by-value lambda capture is copy-initialisation, which explicit forbids. The old
			// std::bind path direct-initialised its stored arguments and so never hit this. The
			// failure is memorable: "cannot convert from 'AvatarConfiguration' to
			// 'AvatarConfiguration'".
			auto arguments = std::make_shared<std::tuple<std::decay_t<B0_>>>(std::forward<B0_>(b0));

			m_asyncWorker(key, [method, arguments](TDatabase& database)
			{
				try
				{
					(static_cast<TBase&>(database).*method)(std::get<0>(*arguments));
				}
				catch (const std::exception& ex)
				{
					defaultLogException(ex);
				}
			});
		}

		/// Calls a returning member function; invokes handler on the io thread. Global key.
		template <class ResultHandler, class TBase, class Result, class... A0, class... Args>
		void asyncRequest(ResultHandler &&handler, Result(TBase::*method)(A0...), Args&&... args)
		{
			asyncRequestKeyed(database_key::Global, std::forward<ResultHandler>(handler), method,
				std::forward<Args>(args)...);
		}

		/// Calls a returning member function, ordered against `key`.
		///
		/// Two calls sharing a key run in the order they were made, which is what lets a write
		/// be followed by a read of the same row.
		template <class ResultHandler, class TBase, class Result, class... A0, class... Args>
		void asyncRequestKeyed(uint64 key, ResultHandler handler, Result(TBase::*method)(A0...), Args&&... args)
		{
			auto resultDispatcher = m_resultDispatcher;

			// See the fire-and-forget overload above for why this is a directly-initialised tuple
			// rather than a by-value capture pack.
			auto arguments = std::make_shared<std::tuple<std::decay_t<Args>...>>(std::forward<Args>(args)...);

			m_asyncWorker(key, [handler, resultDispatcher, method, arguments](TDatabase& database)
			{
				detail::RequestProcessor<Result> processor;
				processor(resultDispatcher,
					[&database, method, &arguments]()
					{
						return std::apply(
							[&database, method](auto&... unpacked)
							{
								return (static_cast<TBase&>(database).*method)(unpacked...);
							},
							*arguments);
					},
					handler);
			});
		}

		/// Calls an arbitrary callable against the database. Global key.
		template <class Result, class ResultHandler, class RequestFunction>
		void asyncRequest(RequestFunction &&request, ResultHandler &&handler)
		{
			asyncRequestKeyed<Result>(database_key::Global, std::forward<RequestFunction>(request),
				std::forward<ResultHandler>(handler));
		}

		/// Calls an arbitrary callable against the database, ordered against `key`.
		template <class Result, class ResultHandler, class RequestFunction>
		void asyncRequestKeyed(uint64 key, RequestFunction request, ResultHandler handler)
		{
			auto resultDispatcher = m_resultDispatcher;
			m_asyncWorker(key, [request, handler, resultDispatcher](TDatabase& database)
			{
				detail::RequestProcessor<Result> processor;
				processor(resultDispatcher, [&database, request]() { return request(&database); }, handler);
			});
		}

		/// Returns the work dispatcher (for constructing narrower wrappers).
		[[nodiscard]] const WorkDispatcher& GetAsyncWorker() const { return m_asyncWorker; }

		/// Returns the result dispatcher (for constructing narrower wrappers).
		[[nodiscard]] const ResultDispatcher& GetResultDispatcher() const { return m_resultDispatcher; }

	private:
		const WorkDispatcher m_asyncWorker;
		const ResultDispatcher m_resultDispatcher;
	};

}
