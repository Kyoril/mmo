// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_login_connector.h"

#include "version.h"

#include "log/default_log_levels.h"

#include <algorithm>
#include <array>

namespace mmo
{
	BotLoginConnector::BotLoginConnector(asio::io_service& io, std::string host, uint16 port)
		: auth::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
		, m_host(std::move(host))
		, m_port(port)
	{
	}

	void BotLoginConnector::SetRealmlist(std::string host, uint16 port)
	{
		m_host = std::move(host);
		m_port = port;
	}

	bool BotLoginConnector::connectionEstablished(bool success)
	{
		if (success)
		{
			// Register for default packet handlers
			RegisterPacketHandler(auth::login_client_packet::LogonChallenge, *this, &BotLoginConnector::OnLogonChallenge);

			// Send the auth packet
			sendSinglePacket([&](auth::OutgoingPacket& packet)
			{
				// Initialize packet using the op code
				packet.Start(auth::client_login_packet::LogonChallenge);

				// Write the actual packet content
				packet
					<< io::write<uint8>(mmo::Major)	// Version
					<< io::write<uint8>(mmo::Minor)
					<< io::write<uint8>(mmo::Build)
					<< io::write<uint16>(mmo::Revision)
					<< io::write<uint32>(auth::ProtocolVersion)
					<< io::write<uint32>(game::ProtocolVersion)
					<< io::write<uint32>(0x64654445)	// Locale: deDE
					<< io::write_dynamic_range<uint8>(m_accountName);

				// Finish packet and send it
				packet.Finish();
			});

			ILOG("[Login] Handshaking...");
		}
		else
		{
			// Connection error!
			ELOG("Could not connect to the login server.");

			// Notify listeners about the connection failure
			AuthenticationResult(auth::AuthResult::FailInvalidServer);
		}
		return true;
	}

	void BotLoginConnector::connectionLost()
	{
		ELOG("Lost connection to the login server");

		// Clear packet handlers
		ClearPacketHandlers();

		m_realms.clear();
	}

	void BotLoginConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}

	PacketParseResult BotLoginConnector::connectionPacketReceived(auth::IncomingPacket& packet)
	{
		return HandleIncomingPacket(packet);
	}

	void BotLoginConnector::DoSRP6ACalculation()
	{
		// Generate a
		m_a.setRand(19 * 8);
		ASSERT(m_a.asUInt32() > 0);

		// Hash generator
		HashGeneratorSha1 gen;

		// Calculate x
		gen.update((const char*)m_s.asByteArray().data(), m_s.getNumBytes());
		gen.update((const char*)m_authHash.data(), m_authHash.size());
		SHA1Hash xHash = gen.finalize();
		m_x.setBinary(xHash.data(), xHash.size());

		// Calculate v
		m_v = constants::srp::g.modExp(m_x, constants::srp::N);

		// Calculate A
		m_A = constants::srp::g.modExp(m_a, constants::srp::N);

		// Calculate u
		SHA1Hash uHash = Sha1_BigNumbers({ m_A, m_B });
		m_u.setBinary(uHash.data(), uHash.size());

		// Calcualte S
		BigNumber k{ 3 };
		m_S = (m_B - k * constants::srp::g.modExp(m_x, constants::srp::N)).modExp((m_a + m_u * m_x), constants::srp::N);
		ASSERT(m_S.asUInt32() > 0);

		// Calculate proof hashes M1 (client) and M2 (server)

		// Split it into 2 seperate strings, interleaved
		// Leave space for terminating 0
		char S1[16 + 1], S2[16 + 1];
		auto arrS = m_S.asByteArray(32);
		for (uint32 i = 0; i < 16; i++)
		{
			S1[i] = arrS[i * 2];
			S2[i] = arrS[i * 2 + 1];
		}

		// Calculate the hash for each string
		gen.update((const char*)S1, 16);
		SHA1Hash S1hash = gen.finalize();
		gen.update((const char*)S2, 16);
		SHA1Hash S2hash = gen.finalize();

		// Re-combine them to form the session key
		uint8 S_hash[40];
		for (uint32 i = 0; i < 20; i++)
		{
			S_hash[i * 2] = S1hash[i];
			S_hash[i * 2 + 1] = S2hash[i];
		}

		// Store the session key as BigNumber so that we can use it for 
		// calculations later on.
		m_sessionKey.setBinary((uint8*)S_hash, 40);

		// Generate hash of plain username
		gen.update((const char*)m_accountName.c_str(), m_accountName.size());
		SHA1Hash userhash2 = gen.finalize();

		// Generate N and g hashes
		SHA1Hash Nhash = Sha1_BigNumbers({ constants::srp::N });
		SHA1Hash ghash = Sha1_BigNumbers({ constants::srp::g });

		// Combine N and g hash like this: (N ^ g)
		uint8 Ng_hash[20];
		for (uint32 i = 0; i < 20; i++) Ng_hash[i] = Nhash[i] ^ ghash[i];

		// Convert hashes into bignumbers so we can calculate easier
		BigNumber t_acc{ userhash2.data(), userhash2.size() };
		BigNumber t_Ng_hash{ Ng_hash, 20 };

		// Caluclate M1 hash sent to the server
		Sha1_Add_BigNumbers(gen, { t_Ng_hash, t_acc, m_s, m_A, m_B });
		gen.update((const char*)S_hash, 40);
		M1hash = gen.finalize();

		// Calculate M2 hash to store for later comparison on server answer
		Sha1_Add_BigNumbers(gen, { m_A });
		gen.update((const char*)M1hash.data(), M1hash.size());
		gen.update((const char*)S_hash, 40);
		M2hash = gen.finalize();
	}

	PacketParseResult BotLoginConnector::OnLogonChallenge(auth::Protocol::IncomingPacket& packet)
	{
		// No longer listen for the logon challenge packet
		ClearPacketHandler(auth::login_client_packet::LogonChallenge);

		// Read the response code
		uint8 result = 0;
		packet >> io::read<uint8>(result);

		// If it was successful, read additional server data
		if (result == auth::auth_result::Success)
		{
			// Read B number
			std::array<uint8, 32> B;
			packet >> io::read_range(B);
			m_B.setBinary(B.data(), B.size());

			// Read and verify g
			uint8 g = 0;
			packet >> io::read<uint8>(g);
			ASSERT(g == constants::srp::g.asUInt32());

			// Read and verify N
			std::array<uint8, 32> N;
			packet >> io::read_range(N);
			BigNumber numN{ N.data(), N.size() };
			ASSERT(numN == constants::srp::N);

			// Read s number (salt)
			std::array<uint8, 32> s;
			packet >> io::read_range(s);
			m_s.setBinary(s.data(), s.size());

			// Read unknown number
			std::array<uint8, 16> unk;
			packet >> io::read_range(unk);
			m_unk.setBinary(unk.data(), unk.size());

			// Do SRP6 calculations
			DoSRP6ACalculation();

			// Listen for proof
			RegisterPacketHandler(auth::login_client_packet::LogonProof, *this, &BotLoginConnector::OnLogonProof);

			// Send proof data to server
			sendSinglePacket([&](auth::OutgoingPacket& packet)
			{
				packet.Start(auth::client_login_packet::LogonProof);
				packet
					<< io::write_range(m_A.asByteArray())
					<< io::write_range(M1hash);
				packet.Finish();
			});

			// No errors yet
			return PacketParseResult::Pass;
		}

		// Authentication failed!
		AuthenticationResult(static_cast<auth::AuthResult>(result));

		// Don't disconnect here - let the caller handle it and close properly
		return PacketParseResult::Pass;
	}

	PacketParseResult BotLoginConnector::OnLogonProof(auth::IncomingPacket& packet)
	{
		// Stop listening to proofs
		ClearPacketHandler(auth::login_client_packet::LogonProof);

		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		if (result == auth::auth_result::Success)
		{
			// Read proof response from server (M2)
			SHA1Hash M2fromServer;
			if (!(packet >> io::read_range(M2fromServer)))
			{
				return PacketParseResult::Disconnect;
			}

			// Compare M2 hashes
			if (memcmp(M2hash.data(), M2fromServer.data(), M2fromServer.size()) != 0)
			{
				AuthenticationResult(auth::AuthResult::FailInternalError);
				return PacketParseResult::Disconnect;
			}

			ILOG("[Login] Handshake successful!");

			// From here on, we accept RealmList packets
			RegisterPacketHandler(mmo::auth::login_client_packet::RealmList, *this, &BotLoginConnector::OnRealmList);

			// Authentication was successful
			AuthenticationResult(auth::AuthResult::Success);
		}
		else
		{
			ILOG("[Login] Auth failed with code " << static_cast<int32>(result));

			if (result <= auth::AuthResult::Count_)
			{
				AuthenticationResult(static_cast<auth::AuthResult>(result));
			}
		}

		// Successfully parsed the packet
		return PacketParseResult::Pass;
	}

	PacketParseResult BotLoginConnector::OnRealmList(auth::IncomingPacket& packet)
	{
		// Clear the realms
		m_realms.clear();

		// Read the realm count
		uint16 realmCount = 0;
		packet >> io::read<uint16>(realmCount);
		m_realms.reserve(realmCount);

		// Notify user about this packet
		ILOG("Available realms: " << realmCount);

		// Read every single realm entry
		for (uint16 i = 0; (i < realmCount) && packet; ++i)
		{
			// Read realm data
			RealmData realm;
			packet
				>> io::read<uint32>(realm.id)
				>> io::read_container<uint8>(realm.name)
				>> io::read_container<uint8>(realm.address)
				>> io::read<uint16>(realm.port);

			// Add to the list of available realms
			m_realms.emplace_back(std::move(realm));
		}

		// Trigger signal
		RealmListUpdated();

		// Continue
		return PacketParseResult::Pass;
	}

	void BotLoginConnector::Connect(const std::string& username, const std::string& password)
	{
		ClearPacketHandlers();

		m_realms.clear();

		// Apply username and convert it to uppercase letters
		m_accountName = username;
		std::transform(m_accountName.begin(), m_accountName.end(), m_accountName.begin(), ::toupper);

		// Apply password in uppercase letters
		std::string upperPassword;
		std::transform(password.begin(), password.end(), std::back_inserter(upperPassword), ::toupper);

		// Calculate auth hash
		const std::string authHash = m_accountName + ":" + upperPassword;
		m_authHash = sha1(authHash.c_str(), authHash.size());

		ILOG("[Login] Connecting to " << m_host << ":" << m_port << "...");

		// Connect to the server
		connect(m_host, m_port, *this, m_ioService);
	}

	void BotLoginConnector::SendRealmListRequest()
	{
		sendSinglePacket([](auth::OutgoingPacket& outPacket)
			{
				outPacket.Start(auth::client_login_packet::RealmList);
				outPacket.Finish();
			});
	}
}
