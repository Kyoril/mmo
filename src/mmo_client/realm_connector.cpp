// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "realm_connector.h"

#include "base/constants.h"
#include "log/default_log_levels.h"

#include <iomanip>

namespace mmo
{
	RealmConnector::RealmConnector(asio::io_service & io)
		: auth::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
	{
	}

	RealmConnector::~RealmConnector()
	{
	}

	void RealmConnector::RegisterPacketHandler(auth::server_packet::Type opCode, PacketHandler handler)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		// Assign packet handler for the given op code
		m_packetHandlers[static_cast<uint8>(opCode)] = std::move(handler);
	}

	void RealmConnector::ClearPacketHandler(auth::server_packet::Type opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(static_cast<uint8>(opCode));
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}

	void RealmConnector::ClearPacketHandlers()
	{
		std::scoped_lock lock{ m_packetHandlerMutex };
		m_packetHandlers.clear();
	}

	bool RealmConnector::connectionEstablished(bool success)
	{
		if (success)
		{
		}
		else
		{
			ELOG("Could not connect to the realm server");
		}

		return true;
	}

	void RealmConnector::connectionLost()
	{
		ELOG("Disconnected");

		// Clear packet handlers
		ClearPacketHandlers();
	}

	void RealmConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}

	PacketParseResult RealmConnector::connectionPacketReceived(auth::IncomingPacket & packet)
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
				WLOG("Received unhandled server op code: 0x" << std::hex << std::setw(2) << static_cast<uint16>(packet.GetId()));
				return PacketParseResult::Disconnect;
			}

			handler = it->second;
		}

		// Call the packet handler
		return handler(packet);
	}

	void RealmConnector::Connect(const std::string& realmAddress, uint16 realmPort, const std::string& realmName, BigNumber sessionKey)
	{
		m_realmAddress = realmAddress;
		m_realmPort = realmPort;
		m_realmName = realmName;
		m_sessionKey = sessionKey;

		// Connect to the server
		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}
}
