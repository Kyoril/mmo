// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "auth_protocol/auth_connector.h"
#include "base/big_number.h"
#include "base/sha1.h"
#include "base/signal.h"

#include "asio/io_service.hpp"

#include <map>
#include <functional>
#include <mutex>


namespace mmo
{
	/// This class manages the connection to the current realm server if there is any.
	class RealmConnector final
		: public auth::Connector
		, public auth::IConnectorListener
	{
	public:
		typedef std::function<PacketParseResult(auth::IncomingPacket &)> PacketHandler;

	public:
		/// Signal that is fired when the client successfully authenticated at the realm list.
		signal<void(auth::AuthResult)> AuthenticationResult;
		/// Signal that is fired when the client received a new character list packet.
		signal<void()> CharListUpdated;

	private:
		// Internal io service
		asio::io_service& m_ioService;

		std::string m_realmAddress;
		uint16 m_realmPort;
		std::string m_realmName;
		BigNumber m_sessionKey;

		/// Active packet handler instances.
		std::map<uint8, PacketHandler> m_packetHandlers;
		std::mutex m_packetHandlerMutex;

	public:
		/// Initializes a new instance of the RealmConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		explicit RealmConnector(asio::io_service &io);
		~RealmConnector();

	public:
		/// Registers a packet handler for a given op code.
		void RegisterPacketHandler(auth::server_packet::Type opCode, PacketHandler handler);
		/// Removes a registered packet handler for a given op code.
		void ClearPacketHandler(auth::server_packet::Type opCode);
		/// Removes all registered packet handlers.
		void ClearPacketHandlers();

	public:
		// ~ Begin IConnectorListener
		virtual bool connectionEstablished(bool success) override;
		virtual void connectionLost() override;
		virtual void connectionMalformedPacket() override;
		virtual PacketParseResult connectionPacketReceived(auth::IncomingPacket &packet) override;
		// ~ End IConnectorListener

	public:
		/// Tries to connect to the given realm server.
		/// @param realmAddress The ip address of the realm.
		/// @param realmPort The port of the realm.
		/// @param realmName The realm's display name.
		void Connect(const std::string& realmAddress, uint16 realmPort, const std::string& realmName, BigNumber sessionKey);
	};
}

