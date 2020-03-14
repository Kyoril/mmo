// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/weak_ptr_function.h"
#include "connection.h"

#include "asio/io_service.hpp"
#include "asio/ip/tcp.hpp"

namespace mmo
{
	template <class P>
	struct IConnectorListener : IConnectionListener<P>
	{
		typedef P Protocol;

		virtual bool connectionEstablished(bool success) = 0;
	};


	template <class P, class MySocket = asio::ip::tcp::socket>
	class Connector 
		: public Connection<P, MySocket>
	{
	public:
		typedef Connection<P> Super;
		typedef P Protocol;
		typedef IConnectorListener<P> Listener;
		typedef MySocket Socket;

	public:
		explicit Connector(std::unique_ptr<Socket> socket, Listener *listener);
		virtual ~Connector();

	public:
		virtual void setListener(Listener &listener);
		Listener *getListener() const;
		void connect(const std::string &host, uint16 port, Listener &listener,
		             asio::io_service &ioService);

		static std::shared_ptr<Connector> create(asio::io_service &service, Listener *listener = nullptr);

	private:

		std::unique_ptr<asio::ip::tcp::resolver> m_resolver;
		uint16 m_port;


		void handleResolve(const asio::system_error &error, asio::ip::tcp::resolver::iterator iterator);
		void beginConnect(asio::ip::tcp::resolver::iterator iterator);
		void handleConnect(const asio::system_error &error, asio::ip::tcp::resolver::iterator iterator);
	};


	template <class P, class MySocket>
	Connector<P, MySocket>::Connector(
	    std::unique_ptr<Socket> socket,
	    Listener *listener)
		: Connection<P, MySocket>(std::move(socket), listener)
		, m_port(0)
	{
	}

	template <class P, class MySocket>
	Connector<P, MySocket>::~Connector()
	{
	}

	template <class P, class MySocket>
	void Connector<P, MySocket>::setListener(Listener &listener)
	{
		Connection<P, MySocket>::setListener(listener);
	}

	template <class P, class MySocket>
	typename Connector<P, MySocket>::Listener *Connector<P, MySocket>::getListener() const
	{
		return dynamic_cast<Listener *>(Connection<P, MySocket>::getListener());
	}

	template <class P, class MySocket>
	void Connector<P, MySocket>::connect(
	    const std::string &host,
		uint16 port,
	    Listener &listener,
	    asio::io_service &ioService)
	{
		setListener(listener);
		assert(getListener());

		m_port = port;

		m_resolver.reset(new asio::ip::tcp::resolver(ioService));

		asio::ip::tcp::resolver::query query(
		    asio::ip::tcp::v4(),
		    host,
		    "");

		const auto this_ = std::static_pointer_cast<Connector<P, MySocket> >(this->shared_from_this());
		assert(this_);

		m_resolver->async_resolve(query,
		                          bind_weak_ptr_2(this_,
		                                  std::bind(&Connector<P, MySocket>::handleResolve,
		                                          std::placeholders::_1,
		                                          std::placeholders::_2,
		                                          std::placeholders::_3)));
	}

	template <class P, class MySocket>
	std::shared_ptr<Connector<P, MySocket> > Connector<P, MySocket>::create(
	    asio::io_service &service,
	    Listener *listener)
	{
		return std::make_shared<Connector<P, MySocket> >(std::unique_ptr<MySocket>(new MySocket(service)), listener);
	}


	template <class P, class MySocket>
	void Connector<P, MySocket>::handleResolve(
	    const asio::system_error &error,
	    asio::ip::tcp::resolver::iterator iterator)
	{
		if (error.code())
		{
			assert(getListener());
			getListener()->connectionEstablished(false);
			this->resetListener();
			return;
		}

		m_resolver.reset();
		beginConnect(iterator);
	}

	template <class P, class MySocket>
	void Connector<P, MySocket>::beginConnect(
	    asio::ip::tcp::resolver::iterator iterator)
	{
		const typename Super::Socket::endpoint_type endpoint(
		    iterator->endpoint().address(),
		    static_cast<std::uint16_t>(m_port)
		);

		const auto this_ = std::static_pointer_cast<Connector<P, MySocket> >(this->shared_from_this());
		assert(this_);

		this->getSocket().lowest_layer().async_connect(endpoint,
		        bind_weak_ptr_1(this_,
		                        std::bind(
		                            &Connector<P, MySocket>::handleConnect,
		                            std::placeholders::_1,
		                            std::placeholders::_2,
		                            ++iterator)));
	}

	template <class P, class MySocket>
	void Connector<P, MySocket>::handleConnect(
	    const asio::system_error &error,
	    asio::ip::tcp::resolver::iterator iterator)
	{
		if (!error.code())
		{
			if (getListener() && getListener()->connectionEstablished(true))
			{
				this->startReceiving();
			}
			return;
		}
		else if (error.code() == asio::error::operation_aborted)
		{
			return;
		}
		else if (iterator == asio::ip::tcp::resolver::iterator())
		{
			if (getListener())
			{
				getListener()->connectionEstablished(false);
				this->resetListener();
			}
			return;
		}

		beginConnect(iterator);
	}
}
