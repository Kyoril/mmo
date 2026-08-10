// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"

#include "asio/ip/tcp.hpp"
#include "asio/steady_timer.hpp"

#include "log/default_log_levels.h"

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <cassert>

namespace mmo
{
	/// Exception type throwed if binding to a specific port failed.
	struct BindFailedException : public std::exception
	{
	};


	/// Basic server class which manages connection.
	template<typename C>
	class Server
	{
	private:

		Server(const Server &Other) = delete;
		Server &operator=(const Server &Other) = delete;

	public:

		typedef C Connection;
		typedef asio::ip::tcp::acceptor AcceptorType;
		typedef signal<void(const std::shared_ptr<Connection> &)> ConnectionSignal;
		typedef std::function<std::shared_ptr<Connection>(asio::io_service &)> ConnectionFactory;

	public:

		/// Default constructor which does nothing.
		Server() { }
		virtual ~Server() { }

		/// Initializes a new server instance and binds it to a specific port.
		Server(asio::io_service &IOService, uint16 Port, ConnectionFactory CreateConnection)
			: m_ioService(IOService)
			, m_createConn(std::move(CreateConnection))
			, m_state(new State(std::unique_ptr<AcceptorType>(new AcceptorType(IOService)), IOService))
		{
			assert(m_createConn);

			try
			{
				m_state->Acceptor->open(asio::ip::tcp::v4());
#if !defined(WIN32) && !defined(_WIN32)
				m_state->Acceptor->set_option(typename AcceptorType::reuse_address(true));
#endif
				m_state->Acceptor->bind(asio::ip::tcp::endpoint(
				                           asio::ip::tcp::v4(),
				                           static_cast<uint16>(Port)));
				// A backlog of 16 drops connections during a login burst; asio's maximum defers to
				// what the OS is willing to queue, which is the right ceiling here.
				m_state->Acceptor->listen(asio::socket_base::max_listen_connections);
			}
			catch (const asio::system_error &)
			{
				throw BindFailedException();
			}
		}
		/// Swap constructor.
		Server(Server &&Other)
		{
			swap(Other);
		}
		/// Swap operator overload.
		Server &operator =(Server &&Other)
		{
			swap(Other);
			return *this;
		}
		/// Swaps contents of one server instance with another server instance.
		void swap(Server &Other)
		{
			std::swap(m_createConn, Other.m_createConn);
			std::swap(m_state, Other.m_state);
		}
		/// Gets the signal which is fired if a new connection was accepted.
		ConnectionSignal &connected()
		{
			return m_state->Connected;
		}
		/// Starts waiting for incoming connections to accept.
		void startAccept()
		{
			assert(m_state);

			if (m_state->Stopped)
			{
				return;
			}

			const std::shared_ptr<Connection> Conn = m_createConn(m_ioService);

			m_state->Acceptor->async_accept(
			    Conn->getSocket().lowest_layer(),
			    std::bind(&Server<C>::Accepted, this, Conn, std::placeholders::_1));
		}

		/// Stops accepting new connections. Connections already handed out are unaffected.
		/// Safe to call more than once.
		void Stop()
		{
			if (!m_state || m_state->Stopped)
			{
				return;
			}

			m_state->Stopped = true;

			asio::error_code error;
			m_state->RetryTimer.cancel(error);
			m_state->Acceptor->close(error);
		}

	private:

		struct State
		{
			std::unique_ptr<AcceptorType> Acceptor;
			ConnectionSignal Connected;

			/// Delays the next accept after a failure. Retrying immediately would spin a core for
			/// as long as the fault lasts -- descriptor exhaustion, typically.
			asio::steady_timer RetryTimer;

			/// Set by Stop(). Read in the accept and retry handlers.
			bool Stopped = false;

			explicit State(std::unique_ptr<AcceptorType> Acceptor_, asio::io_service &IOService)
				: Acceptor(std::move(Acceptor_))
				, RetryTimer(IOService)
			{
			}
		};

		asio::io_service& m_ioService;
		ConnectionFactory m_createConn;
		std::unique_ptr<State> m_state;

		void Accepted(std::shared_ptr<Connection> Conn, const asio::system_error &Error)
		{
			assert(Conn);
			assert(m_state);

			if (m_state->Stopped)
			{
				return;
			}

			if (Error.code())
			{
				if (Error.code() == asio::error::operation_aborted)
				{
					return;
				}

				// Transient failures -- descriptor exhaustion above all -- must not take the
				// listener down permanently. Returning here without re-arming is what let one
				// EMFILE stop a tier from ever accepting again while it kept running and looked
				// healthy.
				ELOG("Accept failed (" << Error.code().message() << "), retrying shortly");

				m_state->RetryTimer.expires_after(std::chrono::milliseconds(100));
				m_state->RetryTimer.async_wait([this](const asio::error_code &timerError)
				{
					if (!timerError && m_state && !m_state->Stopped)
					{
						startAccept();
					}
				});

				return;
			}

			m_state->Connected(Conn);
			startAccept();
		}
	};
}
