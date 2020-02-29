// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_protocol/game_connector.h"
#include "base/big_number.h"
#include "base/sha1.h"
#include "base/signal.h"

#include "asio/io_service.hpp"


namespace mmo
{
	/// This class manages the connection to the current realm server if there is any.
	class RealmConnector final
		: public game::Connector
		, public game::IConnectorListener
	{
	public:
		/// Signal that is fired when the authentication has been successful.
		signal<void()> Authenticated;
		/// Signal that is fired when the client received a new character list packet.
		signal<void()> CharListUpdated;

	private:
		// Internal io service
		asio::io_service& m_ioService;

		std::string m_realmAddress;
		uint16 m_realmPort;
		std::string m_realmName;
		std::string m_account;
		BigNumber m_sessionKey;
		uint32 m_serverSeed;
		uint32 m_clientSeed;

	public:
		/// Initializes a new instance of the RealmConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		explicit RealmConnector(asio::io_service &io);
		~RealmConnector();

	private:
		/// Handles the LogonChallenge packet.
		PacketParseResult OnAuthChallenge(game::IncomingPacket& packet);
		/// Handles the AuthSessionResponse packet.
		PacketParseResult OnAuthSessionResponse(game::IncomingPacket& packet);

	public:
		// ~ Begin IConnectorListener
		virtual bool connectionEstablished(bool success) override;
		virtual void connectionLost() override;
		virtual void connectionMalformedPacket() override;
		virtual PacketParseResult connectionPacketReceived(game::IncomingPacket &packet) override;
		// ~ End IConnectorListener

	public:
		/// Tries to connect to the given realm server.
		/// @param realmAddress The ip address of the realm.
		/// @param realmPort The port of the realm.
		/// @param accountName Name of the player account.
		/// @param realmName The realm's display name.
		void Connect(const std::string& realmAddress, uint16 realmPort, const std::string& accountName, const std::string& realmName, BigNumber sessionKey);
	};
}

