// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "player.h"
#include "player_manager.h"
#include "database.h"
#include "version.h"

#include "base/random.h"
#include "base/constants.h"
#include "base/sha1.h"
#include "base/clock.h"
#include "base/weak_ptr_function.h"
#include "log/default_log_levels.h"

#include <iomanip>
#include <functional>


namespace mmo
{
	Player::Player(
		PlayerManager& playerManager,
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection, 
		const String & address)
		: m_manager(playerManager)
		, m_database(database)
		, m_connection(std::move(connection))
		, m_address(address)
	{
		// Generate random encryption seed
		std::uniform_int_distribution<uint32> dist;
		m_seed = dist(RandomGenerator);

		// Setup ourself as listener
		m_connection->setListener(*this);
	}

	void Player::Destroy()
	{
		m_connection->resetListener();
		m_connection.reset();

		m_manager.PlayerDisconnected(*this);
	}

	void Player::connectionLost()
	{
		ILOG("Client " << m_address << " disconnected");
		Destroy();
	}

	void Player::connectionMalformedPacket()
	{
		ILOG("Client " << m_address << " sent malformed packet");
		Destroy();
	}

	PacketParseResult Player::connectionPacketReceived(auth::IncomingPacket & packet)
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
				WLOG("Packet 0x" << std::hex << (uint16)packetId << " is either unhandled or simply currently not handled");
				return PacketParseResult::Disconnect;
			}

			handler = handlerIt->second;
		}

		// Execute the packet handler and return the result
		return handler(packet);
	}

	PacketParseResult Player::OnAuthSession(auth::IncomingPacket & packet)
	{
		// Read packet data
		if (!(packet
			>> io::read<uint32>(m_build)
			>> io::read_container<uint8>(m_accountName)
			>> io::read<uint32>(m_clientSeed)
			>> io::read_range(m_clientHash)))
		{
			ELOG("Could not read LogonChallenge packet from a game client");
			return PacketParseResult::Disconnect;
		}

		// Verify the client build immediatly for validity
		if (m_build != mmo::Revision)
		{
			ELOG("Unsupported client build " << m_build);
			return PacketParseResult::Disconnect;
		}

		// TODO: Store pending login request data so we can properly handle answers from a login server


		// TODO: Send logon request to the login server


		return PacketParseResult::Pass;
	}

	void Player::SendAuthChallenge()
	{
		// We will start accepting LogonChallenge packets from the client
		RegisterPacketHandler(auth::client_realm_packet::AuthSession, *this, &Player::OnAuthSession);

		// Send the LogonChallenge packet to the client including our generated seed
		m_connection->sendSinglePacket([this](mmo::auth::OutgoingPacket& packet) {
			packet.Start(mmo::auth::realm_client_packet::AuthChallenge);
			packet << io::write<uint32>(m_seed);
			packet.Finish();
		});
	}

	void Player::RegisterPacketHandler(uint8 opCode, PacketHandler && handler)
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

	void Player::ClearPacketHandler(uint8 opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}
}
