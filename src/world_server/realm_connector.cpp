// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "realm_connector.h"

#include "base/constants.h"
#include "log/default_log_levels.h"


namespace mmo
{
	RealmConnector::RealmConnector(asio::io_service& io)
		: auth::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
	{
	}

	RealmConnector::~RealmConnector()
	{
	}

	PacketParseResult RealmConnector::connectionPacketReceived(auth::IncomingPacket& packet)
	{
		return HandleIncomingPacket(packet);
	}

	bool RealmConnector::connectionEstablished(bool success)
	{
		if (success)
		{
			ILOG("[Login] Handshaking...");
		}
		else
		{
			// Connection error!
			ELOG("Could not connect to the realm server.");
		}
		return true;
	}

	void RealmConnector::connectionLost()
	{
		ELOG("Lost connection to the realm server");

		// Clear packet handlers
		ClearPacketHandlers();
	}

	void RealmConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}
}
