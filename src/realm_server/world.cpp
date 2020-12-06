// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "world.h"
#include "database.h"

#include "base/random.h"
#include "base/weak_ptr_function.h"
#include "log/default_log_levels.h"

#include <functional>


namespace mmo
{
	World::World(
		WorldManager& worldManager,
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection, 
		const String & address)
		: m_manager(worldManager)
		, m_database(database)
		, m_connection(std::move(connection))
		, m_address(address)
	{
		// Setup ourself as listener
		m_connection->setListener(*this);
	}

	void World::Destroy()
	{
		m_connection->resetListener();
		m_connection.reset();

		m_manager.WorldDisconnected(*this);
	}

	void World::connectionLost()
	{
		ILOG("World node " << m_address << " disconnected");
		Destroy();
	}

	void World::connectionMalformedPacket()
	{
		ILOG("World node " << m_address << " sent malformed packet");
		Destroy();
	}

	PacketParseResult World::connectionPacketReceived(game::IncomingPacket & packet)
	{
		const auto packetId = packet.GetId();
		bool isValid = true;

		PacketHandler handler = nullptr;

		{
			// Lock packet handler access
			std::scoped_lock lock{ m_packetHandlerMutex };

			// Check for packet handlers
			auto handlerIt = m_packetHandlers.find(packetId);
			if (handlerIt == m_packetHandlers.end())
			{
				WLOG("Packet 0x" << std::hex << packetId << " is either unhandled or simply currently not handled");
				return PacketParseResult::Disconnect;
			}

			handler = handlerIt->second;
		}

		// Execute the packet handler and return the result
		return handler(packet);
	}

	void World::RegisterPacketHandler(uint16 opCode, PacketHandler && handler)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it == m_packetHandlers.end())
		{
			m_packetHandlers.emplace(std::make_pair(opCode, std::forward<PacketHandler>(handler)));
		}
		else
		{
			it->second = std::forward<PacketHandler>(handler);
		}
	}

	void World::ClearPacketHandler(uint16 opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}
}
