// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "auth_protocol/auth_connector.h"

#include "asio/io_service.hpp"


namespace mmo
{
	/// A connector which will try to log in to a realm server.
	class RealmConnector final
		: public auth::Connector
		, public auth::IConnectorListener
	{
	private:
		// Internal io service
		asio::io_service& m_ioService;

	public:
		/// Initializes a new instance of the TestConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		explicit RealmConnector(asio::io_service& io);
		~RealmConnector();

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(auth::IncomingPacket& packet) override;
		// ~ End IConnectorListener
	};
}

