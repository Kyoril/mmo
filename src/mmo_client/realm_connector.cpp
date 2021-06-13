// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "realm_connector.h"
#include "version.h"

#include "base/constants.h"
#include "base/random.h"
#include "base/sha1.h"
#include "log/default_log_levels.h"


namespace mmo
{
	RealmConnector::RealmConnector(asio::io_service & io)
		: game::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
		, m_realmPort(0)
		, m_serverSeed(0)
		, m_clientSeed(0)
	{
	}

	PacketParseResult RealmConnector::OnAuthChallenge(game::IncomingPacket & packet)
	{
		// No longer handle LogonChallenge packets during this session
		ClearPacketHandler(game::realm_client_packet::AuthChallenge);

		// Try to read the packet data
		if (!(packet >> io::read<uint32>(m_serverSeed)))
		{
			return PacketParseResult::Disconnect;
		}

		// Calculate a hash for verification
		HashGeneratorSha1 hashGen;
		hashGen.update(m_account.data(), m_account.length());
		hashGen.update(reinterpret_cast<const char*>(&m_clientSeed), sizeof(m_clientSeed));
		hashGen.update(reinterpret_cast<const char*>(&m_serverSeed), sizeof(m_serverSeed));
		Sha1_Add_BigNumbers(hashGen, {m_sessionKey});
		SHA1Hash hash = hashGen.finalize();

		// Listen for response packet
		RegisterPacketHandler(game::realm_client_packet::AuthSessionResponse, *this, &RealmConnector::OnAuthSessionResponse);

		// We have been challenged, respond with an answer
		sendSinglePacket([this, &hash](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AuthSession);
			packet
				<< io::write<uint32>(mmo::Revision)
				<< io::write_dynamic_range<uint8>(this->m_account)
				<< io::write<uint32>(m_clientSeed)
				<< io::write_range(hash);
			packet.Finish();
		});

		// Initialize connection encryption afterwards
		HMACHash cryptKey;
		GetCrypt().GenerateKey(cryptKey, m_sessionKey);
		GetCrypt().SetKey(cryptKey.data(), cryptKey.size());
		GetCrypt().Init();

		// Debug log
		ILOG("[Realm] Handshaking...");

		// Proceed handling network packets
		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnAuthSessionResponse(game::IncomingPacket & packet)
	{
		// No longer accept these packets from here on!
		ClearPacketHandler(game::realm_client_packet::AuthSessionResponse);

		// Try to read packet data
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		// Authentication has been successful!
		AuthenticationResult(result);

		// Was this a success?
		if (result == game::auth_result::Success)
		{
			// From here on, we accept CharEnum packets
			RegisterPacketHandler(game::realm_client_packet::CharEnum, *this, &RealmConnector::OnCharEnum);
			RegisterPacketHandler(game::realm_client_packet::NewWorld, *this, &RealmConnector::OnNewWorld);
			RegisterPacketHandler(game::realm_client_packet::EnterWorldFailed, *this, &RealmConnector::OnEnterWorldFailed);

			// And now, we ask for the character list
			sendSinglePacket([](game::OutgoingPacket& outPacket)
			{
				outPacket.Start(game::client_realm_packet::CharEnum);
				// This packet is empty
				outPacket.Finish();
			});
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnCharEnum(game::IncomingPacket & packet)
	{
		// Delete old characters in cache
		m_characterViews.clear();

		// Load new character view list
		if (!(packet >> io::read_container<uint8>(m_characterViews)))
		{
			return PacketParseResult::Disconnect;
		}

		// Notify about character list update
		CharListUpdated();

		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnNewWorld(game::IncomingPacket& packet)
	{
		DLOG("New world packet received");
		
		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnEnterWorldFailed(game::IncomingPacket& packet)
	{
		ELOG("Failed to enter world!");
		
		return PacketParseResult::Pass;
	}

	bool RealmConnector::connectionEstablished(bool success)
	{
		if (success)
		{
			// Reset server seed
			m_serverSeed = 0;

			// Generate a new client seed
			std::uniform_int_distribution<uint32> dist;
			m_clientSeed = dist(RandomGenerator);

			// Accept LogonChallenge packets from here on
			RegisterPacketHandler(game::realm_client_packet::AuthChallenge, *this, &RealmConnector::OnAuthChallenge);
		}
		else
		{
			ELOG("Could not connect to the realm server");
		}

		return true;
	}

	void RealmConnector::connectionLost()
	{
		// Log this event
		ELOG("Lost connection to the realm server...");

		// Clear packet handlers
		ClearPacketHandlers();

		Disconnected();
	}

	void RealmConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}

	PacketParseResult RealmConnector::connectionPacketReceived(game::IncomingPacket & packet)
	{
		return HandleIncomingPacket(packet);
	}

	void RealmConnector::SetLoginData(const std::string & accountName, const BigNumber & sessionKey)
	{
		m_account = accountName;
		m_sessionKey = sessionKey;
	}

	void RealmConnector::ConnectToRealm(const RealmData & data)
	{
		m_realmAddress = data.address;
		m_realmPort = data.port;
		m_realmName = data.name;

		// Connect to the server
		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}

	void RealmConnector::Connect(const std::string& realmAddress, uint16 realmPort, const std::string& accountName, const std::string& realmName, BigNumber sessionKey)
	{
		m_realmAddress = realmAddress;
		m_realmPort = realmPort;
		m_realmName = realmName;
		m_account = accountName;
		m_sessionKey = sessionKey;

		// Connect to the server
		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}

	void RealmConnector::EnterWorld(const CharacterView & character)
	{
		const uint64 guid = character.GetGuid();

		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::EnterWorld);
			packet
				<< io::write<uint64>(guid);
			packet.Finish();
		});
	}

	void RealmConnector::CreateCharacter(const std::string& name)
	{
		sendSinglePacket([&name](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CreateChar);
			packet << io::write_dynamic_range<uint8>(name);
			packet.Finish();
			});
	}

	void RealmConnector::DeleteCharacter(const CharacterView& character)
	{
		const uint64 guid = character.GetGuid();

		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::DeleteChar);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}
}
