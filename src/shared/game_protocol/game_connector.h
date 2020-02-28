// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "game_protocol.h"
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
		/// Basic connector implementation using the Auth protocol. Extends the Connector class
		/// by thread safe PacketHandler management methods.
		class GameConnector 
			: public mmo::Connector<Protocol>
		{
		public:
			typedef std::function<PacketParseResult(game::IncomingPacket &)> PacketHandler;

		public:
			explicit GameConnector(std::unique_ptr<Socket> socket, Listener *listener)
				: mmo::Connector<Protocol>(std::move(socket), listener)
			{
			}
			virtual ~GameConnector() = default;

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
		};

		typedef GameConnector Connector;
		typedef mmo::IConnectorListener<Protocol> IConnectorListener;
	}
}
