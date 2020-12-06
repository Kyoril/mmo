// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "world_manager.h"

#include "base/non_copyable.h"
#include "game_protocol/game_protocol.h"
#include "game_protocol/game_connection.h"

#include <memory>
#include <functional>
#include <map>
#include <cassert>


namespace mmo
{
	class AsyncDatabase;


	/// This class represents a player connction on the login server.
	class World final
		: public NonCopyable
		, public game::IConnectionListener
		, public std::enable_shared_from_this<World>
	{
	public:
		typedef game::EncryptedConnection<game::Protocol> Client;
		typedef std::function<PacketParseResult(game::IncomingPacket &)> PacketHandler;

	public:
		explicit World(
			WorldManager &manager,
			AsyncDatabase &database,
			std::shared_ptr<Client> connection,
			const std::string &address);

		/// Gets the player connection class used to send packets to the client.
		inline Client &GetConnection() { assert(m_connection); return *m_connection; }
		/// Gets the world manager which manages all connected world instances.
		inline WorldManager &GetManager() const { return m_manager; }

	public:
		/// Registers a packet handler.
		void RegisterPacketHandler(uint16 opCode, PacketHandler &&handler);
		/// Syntactic sugar implementation of RegisterPacketHandler to avoid having to use std::bind.
		template <class Instance, class Class, class... Args1>
		void RegisterPacketHandler(uint16 opCode, Instance& object, PacketParseResult(Class::*method)(Args1...))
		{
			RegisterPacketHandler(opCode, [&object, method](Args1... args) {
				return (object.*method)(Args1(args)...);
			});
		}
		/// Clears a packet handler so that the opcode is no longer handled.
		void ClearPacketHandler(uint16 opCode);

	private:
		WorldManager &m_manager;
		AsyncDatabase &m_database;
		std::shared_ptr<Client> m_connection;
		std::string m_address;						// IP address in string format
		std::map<uint16, PacketHandler> m_packetHandlers;
		std::mutex m_packetHandlerMutex;

	private:
		/// Closes the connection if still connected.
		void Destroy();

	private:
		/// @copydoc mmo::auth::IConnectionListener::connectionLost()
		void connectionLost() override;
		/// @copydoc mmo::auth::IConnectionListener::connectionMalformedPacket()
		void connectionMalformedPacket() override;
		/// @copydoc mmo::auth::IConnectionListener::connectionPacketReceived()
		PacketParseResult connectionPacketReceived(game::IncomingPacket &packet) override;
	};
}