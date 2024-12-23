// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"

#include "asio/ip/tcp.hpp"

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
			, m_state(new State(std::unique_ptr<AcceptorType>(new AcceptorType(IOService))))
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
				m_state->Acceptor->listen(16);
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
			const std::shared_ptr<Connection> Conn = m_createConn(m_ioService);

			m_state->Acceptor->async_accept(
			    Conn->getSocket().lowest_layer(),
			    std::bind(&Server<C>::Accepted, this, Conn, std::placeholders::_1));
		}

	private:

		struct State
		{
			std::unique_ptr<AcceptorType> Acceptor;
			ConnectionSignal Connected;

			explicit State(std::unique_ptr<AcceptorType> Acceptor_)
				: Acceptor(std::move(Acceptor_))
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

			if (Error.code())
			{
				//TODO
				return;
			}

			m_state->Connected(Conn);
			startAccept();
		}
	};
}
