// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "game_protocol.h"
#include "game_connection.h"
#include "game_crypt.h"

#include "network/connector.h"

#include "log/default_log_levels.h"

#include <functional>
#include <iomanip>
#include <mutex>
#include <map>


namespace mmo
{
	namespace game
	{
		template <class P, class MySocket = asio::ip::tcp::socket>
		class EncryptedConnector
			: public game::EncryptedConnection<P, MySocket>
		{
		public:
			typedef std::function<PacketParseResult(game::IncomingPacket &)> PacketHandler;

		public:
			typedef EncryptedConnection<P, MySocket> Super;
			typedef EncryptedConnector<P, MySocket> ThisClass;
			typedef P Protocol;
			typedef mmo::IConnectorListener<P> Listener;
			typedef MySocket Socket;

		public:
			explicit EncryptedConnector(std::unique_ptr<Socket> socket, Listener *listener)
				: Super(std::move(socket), listener)
				, m_port(0)
			{
			}
			virtual ~EncryptedConnector() = default;

		public:
			Listener *GetListener() const
			{
				return dynamic_cast<Listener *>(Super::getListener());
			}
			
			void SetListener(Listener& _listener)
			{
				return Super::setListener(dynamic_cast<typename Super::Listener&>(_listener));
			}

		public:
			/// Registers a packet handler for a given op code.
			void RegisterPacketHandler(uint16 opCode, PacketHandler&& handler)
			{
				std::scoped_lock lock{ m_packetHandlerMutex };
				m_packetHandlers[opCode] = handler;
			}
			/// Syntactic sugar implementation of RegisterPacketHandler to avoid having to use std::bind.
			template <class Instance, class Class, class... Args1>
			void RegisterPacketHandler(uint16 opCode, Instance& object, PacketParseResult(Class::*method)(Args1...))
			{
				RegisterPacketHandler(opCode, [&object, method](Args1... args) {
					return (object.*method)(Args1(args)...);
				});
			}
			/// Removes a registered packet handler for a given op code.
			void ClearPacketHandler(uint16 opCode)
			{
				std::scoped_lock lock{ m_packetHandlerMutex };

				auto it = m_packetHandlers.find(opCode);
				if (it != m_packetHandlers.end())
				{
					m_packetHandlers.erase(it);
				}
			}
			/// Removes all registered packet handlers.
			void ClearPacketHandlers()
			{
				std::scoped_lock lock{ m_packetHandlerMutex };
				m_packetHandlers.clear();
			}

		protected:
			virtual PacketParseResult HandleIncomingPacket(game::IncomingPacket &packet)
			{
				// Try to retrieve the packet handler in a thread-safe way
				PacketHandler handler = nullptr;
				{
					std::scoped_lock lock{ m_packetHandlerMutex };

					// Try to find a registered packet handler
					auto it = m_packetHandlers.find(packet.GetId());
					if (it == m_packetHandlers.end())
					{
						// Received unhandled server packet
						WLOG("Received unhandled server op code: 0x" << std::hex << std::setw(2) << std::setfill('0') << packet.GetId());
						return PacketParseResult::Disconnect;
					}

					handler = it->second;
				}

				// Call the packet handler
				return handler(packet);
			}

		protected:
			std::mutex m_packetHandlerMutex;
			std::map<uint16, PacketHandler> m_packetHandlers;

		public:
			void connect(const std::string &host, uint16 port, Listener &listener,
				asio::io_service &ioService)
			{
				SetListener(listener);
				ASSERT(GetListener());

				m_port = port;

				m_resolver.reset(new asio::ip::tcp::resolver(ioService));

				asio::ip::tcp::resolver::query query(
					asio::ip::tcp::v4(),
					host,
					"");

				const auto this_ = std::static_pointer_cast<ThisClass>(this->shared_from_this());
				ASSERT(this_);

				m_resolver->async_resolve(query,
					bind_weak_ptr_2(this_,
						std::bind(&ThisClass::handleResolve,
							std::placeholders::_1,
							std::placeholders::_2,
							std::placeholders::_3)));
			}

			static std::shared_ptr<EncryptedConnector> Create(asio::io_service &service, Listener *listener = nullptr)
			{
				return std::make_shared<Super>(std::unique_ptr<MySocket>(new MySocket(service)), listener);
			}

		private:
			std::unique_ptr<asio::ip::tcp::resolver> m_resolver;
			uint16 m_port;

		private:
			void handleResolve(const asio::system_error &error, asio::ip::tcp::resolver::iterator iterator)
			{
				if (error.code())
				{
					assert(GetListener());
					GetListener()->connectionEstablished(false);
					this->resetListener();
					return;
				}

				m_resolver.reset();
				beginConnect(iterator);
			}
			void beginConnect(asio::ip::tcp::resolver::iterator iterator)
			{
				const typename Super::Socket::endpoint_type endpoint(
					iterator->endpoint().address(),
					static_cast<std::uint16_t>(m_port)
				);

				const auto this_ = std::static_pointer_cast<ThisClass>(this->shared_from_this());
				assert(this_);

				this->getSocket().lowest_layer().async_connect(endpoint,
					bind_weak_ptr_1(this_,
						std::bind(
							&ThisClass::handleConnect,
							std::placeholders::_1,
							std::placeholders::_2,
							++iterator)));
			}
			void handleConnect(const asio::system_error &error, asio::ip::tcp::resolver::iterator iterator)
			{
				if (!error.code())
				{
					if (GetListener()->connectionEstablished(true))
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
					if (GetListener())
					{
						GetListener()->connectionEstablished(false);
						this->resetListener();
					}
					return;
				}

				beginConnect(iterator);
			}
		};

		typedef EncryptedConnector<Protocol> Connector;
		typedef mmo::IConnectorListener<Protocol> IConnectorListener;
	}
}
